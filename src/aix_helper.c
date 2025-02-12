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

#include <stdio.h>
// #include <dirent.h>
#include <ctype.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h> // for close
// #include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h> //for basename and dirname
#include <sys/ioctl.h>

#include <odmi.h>
#include <sys/cfgodm.h>
#include <sys/devinfo.h> //to read info about device/controller from openned handle
#include <sys/ide.h>
#include <sys/scdisk.h>
#include <sys/scsi.h>
#include <sys/scsi_buf.h>
#if defined(DF_NVME) // this will be defined in devinfo.h if NVMe devices are supported(should be in AIX7 and up)
#    include <sntl_helper.h>
#    include <sys/nvme.h>
#else
#    if !defined(DISABLE_NVME_PASSTHROUGH)
#        define DISABLE_NVME_PASSTHROUGH
#    endif
#endif

// #include <mntent.h> //for determining mounted file systems
#include "aix_helper.h"
#include "bit_manip.h"
#include "cmds.h"
#include "code_attributes.h"
#include "error_translation.h"
#include "math_utils.h"
#include "memory_safety.h"
#include "precision_timer.h"
#include "string_utils.h"
#include "type_conversion.h"

bool os_Is_Infinite_Timeout_Supported(void)
{
    return false;
}

extern bool validate_Device_Struct(versionBlock);

// http://ps-2.kev009.com/tl/techlib/manuals/adoclib/aixprggd/kernextc/scsisubs.htm#A350C983af

// File systems are /dev/hd?, but block disks are /dev/hdisk? and raw disks are /dev/rhdisk?
// There is some way these are related, but I'm not sure how to get the mapping between them.
// The code below will compile and run, but you need to know what input to give it to do anything useful.
// If we can figure out how /dev/hd is on /dev/hdisk or the other way around, this should be usable, or can be modified
// to work better-TJE

// static int get_Partition_Count(const char * blockDeviceName)
// {
//     int result = 0;
//     FILE *mount = setmntent(MOUNTED, "r");//MNTTAB or MOUNTED. MOUNTED is virtual file
//     struct mntent *entry = M_NULLPTR;
//     while(M_NULLPTR != (entry = getmntent(mount)))
//     {
//         if(strstr(entry->mnt_fsname, blockDeviceName))
//         {
//             //Found a match, increment result counter.
//             ++result;
//         }
//     }
//     endmntent(mount);
//     return result;
// }

// #define PART_INFO_NAME_LENGTH (32)
// #define PART_INFO_PATH_LENGTH (64)
// typedef struct s_spartitionInfo
// {
//     char fsName[PART_INFO_NAME_LENGTH];
//     char mntPath[PART_INFO_PATH_LENGTH];
// }spartitionInfo, *ptrsPartitionInfo;
// //partitionInfoList is a pointer to the beginning of the list
// //listCount is the number of these structures, which should be returned by get_Partition_Count
// static eReturnValues get_Partition_List(const char * blockDeviceName, ptrsPartitionInfo partitionInfoList, int
// listCount)
// {
//     int result = SUCCESS;
//     int matchesFound = 0;
//     if(listCount > 0)
//     {
//         FILE *mount = setmntent(MOUNTED, "r");//MNTTAB or MOUNTED. MOUNTED is virtual file
//         struct mntent *entry = M_NULLPTR;
//         while(M_NULLPTR != (entry = getmntent(mount)))
//         {
//             if(strstr(entry->mnt_fsname, blockDeviceName))
//             {
//                 //found a match, copy it to the list
//                 if(matchesFound < listCount)
//                 {
//                     snprintf_err_handle((partitionInfoList + matchesFound)->fsName, PART_INFO_NAME_LENGTH, "%s",
//                     entry->mnt_fsname); snprintf_err_handle((partitionInfoList + matchesFound)->mntPath,
//                     PART_INFO_PATH_LENGTH,
//                     "%s", entry->mnt_dir);
//                     ++matchesFound;
//                 }
//                 else
//                 {
//                     result = MEMORY_FAILURE;//out of memory to copy all results to the list.
//                 }
//             }
//         }
//         endmntent(mount);
//     }
//     return result;
// }

// //in future, when code is written, use internal API used by the other FS/unmount options to figure this out.-TJE
// static int set_Device_Partition_Info(M_ATTR_UNUSED tDevice* device)
// {
//     return NOT_SUPPORTED;
// }

// This may be useful for debug level prints - TJE
static void print_devinfo_struct(struct devinfo* devInfoData)
{
    // this does it's best to parse and print in a human readable for the devinfo
    // structure based on all the known/reported flags in devinfo.h
    if (devInfoData != M_NULLPTR)
    {
        printf("struct devinfo:\n");
        printf("\tdevtype = %c", devInfoData->devtype);
        switch (devInfoData->devtype)
        {
        case DD_TMSCSI:
            printf(" - SCSI target mode\n");
            break;
        case DD_SCSITM:
            printf(" - SCSI-3 target mode\n");
            break;
        case DD_LP:
            printf(" - line printer\n");
            break;
        case DD_TAPE:
            printf(" - mag tape\n");
            break;
        case DD_SCTAPE:
            printf(" - SCSI tape\n");
            break;
        // case DD_TTY:
        //     printf(" - terminal\n");
        //     break;
        case DD_DISK:
            printf(" - disk\n");
            break;
        case DD_CDROM:
            printf(" - cdrom\n");
            break;
        case DD_DLC:
            printf(" - Data Link Control\n");
            break;
        case DD_SCDISK:
            printf(" - SCSI disk\n"); /* SCSI disk, but NVMe disk if DF_NVME is set */
            break;
        case DD_RTC:
            printf(" - real-time (calendar) clock\n");
            break;
        case DD_PSEU:
            printf(" - psuedo device\n");
            break;
        case DD_NET:
            printf(" - networks\n");
            break;
        case DD_EN:
            printf(" - Ethernet Interface\n");
            break;
        case DD_EM78:
            printf(" - 3278/79 emulator\n");
            break;
        case DD_TR:
            printf(" - token ring\n");
            break;
        case DD_BIO:
            printf(" - block i/o device\n");
            break;
        case DD_X25:
            printf(" - X.25 DDN device driver\n");
            break;
        case DD_IEEE_3:
            printf(" - IEEE 802.3\n");
            break;
        case DD_SL:
            printf(" - Serial line IP\n");
            break;
        case DD_LO:
            printf(" - Loopback IP\n");
            break;
        case DD_DUMP:
            printf(" - dump device driver\n");
            break;
        // case DD_SCCD:
        //     printf(" - SCSI CDROM\n");
        //     break;
        case DD_CIO:
            printf(" - common communications device driver\n");
            break;
        case DD_BUS:
            printf(" - I/O Bus device\n");
            break;
        case DD_HFT:
            printf(" - HFT\n");
            break;
        case DD_INPUT:
            printf(" - graphic input device\n");
            break;
        case DD_CON:
            printf(" - console\n");
            break;
        case DD_NET_DH:
            printf(" - Network device handler\n");
            break;
        case DD_DISK_C:
            printf(" - Disk Controller\n");
            break;
        case DD_SOL:
            printf(" - Serial Optical Link\n");
            break;
        case DD_CAT:
            printf(" - S/370 parallel channel\n");
            break;
        case DD_FDDI:
            printf(" - FDDI\n");
            break;
        case DD_SCRWOPT:
            printf(" - SCSI R/W optical\n");
            break;
        case DD_SES:
            printf(" - SCSI Enclosure Services Device\n");
            break;
        case DD_AUDIT:
            printf(" - Streams mode auditing virtual device\n");
            break;
        case DD_LIB:
            printf(" - Medium library device\n");
            break;
        case DD_VIOA:
            printf(" - Virtual IOA\n");
            break;
        case DD_OTHER:
            printf(" - Other\n");
            break;
        default:
            printf(" - Unknown\n");
            break;
        }
        printf("\tdevsubtype = %c", devInfoData->devsubtype);
        switch (devInfoData->devsubtype)
        {
        case DS_DLCETHER: /* DLC - Standard Ethernet */
            printf(" - DLC - Standard Ethernet\n");
            break;
        case DS_DLC8023: /* DLC - IEEE 802.3 Ethernet */
            printf(" - DLC - IEEE 802.3 Ethernet\n");
            break;
        case DS_DLCTOKEN: /* DLC - Token Ring */
            printf(" - DLC - Token Ring\n");
            break;
        case DS_DLCSDLC: /* DLC - SDLC */
            printf(" - DLC - SDLC\n");
            break;
        case DS_DLCQLLC: /* DLC - X.25 Qualified LLC */
            printf(" - DLC - X.25 Qualified LLC\n");
            break;
        case DS_DLCFDDI: /* DLC - FDDI */
            printf(" - DLC - FDDI\n");
            break;
        case DS_LV: /* logical volume */
            printf(" - logical volume\n");
            break;
        case DS_PV: /* physical volume - hard disk */
            printf(" - physical volume - hard disk\n");
            break;
        case DS_SCSI: /* SCSI adapter */
            printf(" - SCSI Adapter\n");
            break;
        case DS_IDE: /* IDE adapter  */
            printf(" - IDE adapter\n");
            break;
        case DS_SAS: /* SAS adapter  */
            printf(" - SAS adapter\n");
            break;
        case DS_SATA: /* SATA adapter */
            printf(" - SATA adapter\n");
            break;
        // case DS_PP:     /* Parallel printer */
        //     printf(" - Parallel printer\n");
        //     break;
        case DS_SP: /* Serial printer   */
            printf(" - Serial Printer\n");
            break;
        case DS_TM: /* SCSI target mode */
            printf(" - SCSI target mode\n");
            break;
        case DS_SDA: /* Serial DASD adapter */
            printf(" - Serial DASD adapter\n");
            break;
        case DS_SDC: /* Serial DASD Controller */
            printf(" - Serial DASD Controller\n");
            break;
        case DS_NFS: /* NFS device for swapping */
            printf(" - NFS Device for Swapping\n");
            break;
        case DS_CAT: /* S/370 parallel channel */
            printf(" - S/370 Parallel Channel\n");
            break;
        case DS_FCP: /* FC SCSI adapter        */
            printf(" - FC SCSI Adapter\n");
            break;
#if !defined(DISABLE_NVME_PASSTHROUGH)
        case DS_FCNVME: /* FC-NVMe device       */
            printf(" - FC-NVMe device\n");
            break;
#endif              // DISABLE_NVME_PASSTHROUGH
        case DS_VM: /* VM logical volume */
            printf(" - VM Logical volume\n");
            break;
        // case DS_QIO:     /* Quick IO logical volume */
        //     printf(" - Quick IO logical volume\n");
        //     break;
        case DS_ISCSI: /* iSCSI adapter           */
            printf(" - iSCSI adapter\n");
            break;
        // case DS_LVZ:	/* New logical volume type */
        //     M_FALLTHROUGH;
        case DS_LV0: /* New logical volume type */
            printf(" - New logical volume type\n");
            break;
        case DS_VMZ: /* VM logical volume */
            printf(" - VM logical volume\n");
            break;
        case DS_VDEVICE: /* Virtual deivce or bus   */
            printf(" - Virtual device or bus\n");
            break;
        // case DS_CVSCSI:   /* Virtual SCSI Client (hosteD)  */
        //     printf(" - Virtual SCSI Client (hosteD)\n");
        //     break;
        case DS_SVSCSI: /* Virtual SCSI Server (hostinG) */
            printf(" - Virtual SCSI Server (hostinG)\n");
            break;
        case DS_RPV: /* Remote device */
            printf(" - Remote device\n");
            break;
        case DS_ACCEL: /* Accel device */
            printf(" - Accel device\n");
            break;
#if defined(DS_CAPI_IO)
        case DS_CAPI_IO: /* CAPI Storage device */
            printf(" - CAPI Storage device\n");
            break;
#endif // DS_CAPI_IO
#if defined(DS_VRTSCSI)
        case DS_VRTSCSI: /* VirtIO SCSI Client Adapter */
            printf(" - VirtIO SCSI Client Adapter\n");
            break;
#endif               // DS_VRTSCSI
        case DS_VSD: /* VSD type device */
            printf(" - VSD Type Device\n");
            break;
#if !defined(DISABLE_NVME_PASSTHROUGH)
        case DS_NVME: /* non-volatile Memory controller   */
            printf(" - Non-Volatile Memory Controller (NVMe)\n");
            break;
#endif // DISABLE_NVME_PASSTHROUGH
        default:
            printf(" - Unknown\n");
            break;
        }
        printf("\tflags = %02" PRIX8 "h\n", devInfoData->flags);
        if (devInfoData->flags & DF_FIXED)
        {
            printf("\t\tFixed/non-removable\n");
        }
        if (devInfoData->flags & DF_RAND)
        {
            printf("\t\tRandom access possible\n");
        }
        if (devInfoData->flags & DF_FAST)
        {
            printf("\t\tFast\n");
        }
        if (devInfoData->flags & DF_CONC)
        {
            printf("\t\tConcurrent mode supported\n");
        }
        if (devInfoData->flags & DF_LGDSK)
        {
            printf("\t\tLarge > 2TB disk\n");
        }
        if (devInfoData->flags & DF_IVAL)
        {
            printf("\t\tInner structure flags are valid\n");
        }
        // based on type/subtype and flags, check proper structure in union called un
        switch (devInfoData->devtype)
        {
        case DD_TMSCSI:
            // devInfoData->un.tmscsi
            printf("\t\ttmscsi");
            printf("\t\t\tscsi_id: %" PRIu8 "\n", devInfoData->un.tmscsi.scsi_id);
            printf("\t\t\tlun_id: %" PRIu8 "\n", devInfoData->un.tmscsi.lun_id);
            printf("\t\t\tbuf_size: %" PRIu32 "\n", devInfoData->un.tmscsi.buf_size);
            printf("\t\t\tnum_bufs: %" PRIu32 "\n", devInfoData->un.tmscsi.num_bufs);
            printf("\t\t\tmax_transfer: %ld\n", devInfoData->un.tmscsi.max_transfer);
            printf("\t\t\tadapter_devno: %" PRIu32 "\n", devInfoData->un.tmscsi.adapter_devno);
            break;
        case DD_SCSITM:
            // devInfoData->un.scsitm
            printf("\t\tscsitm");
            printf("\t\t\tlo_scsi_id: %" PRIu8 "\n", devInfoData->un.scsitm.lo_scsi_id);
            printf("\t\t\thi_scsi_id: %" PRIu8 "\n", devInfoData->un.scsitm.hi_scsi_id);
            printf("\t\t\tlo_lun_id: %" PRIu8 "\n", devInfoData->un.scsitm.lo_lun_id);
            printf("\t\t\thi_lun_id: %" PRIu8 "\n", devInfoData->un.scsitm.hi_lun_id);
            printf("\t\t\tbuf_size: %" PRIu32 "\n", devInfoData->un.scsitm.buf_size);
            printf("\t\t\tnum_bufs: %" PRIu32 "\n", devInfoData->un.scsitm.num_bufs);
            printf("\t\t\tmax_transfer: %ld\n", devInfoData->un.scsitm.max_transfer);
            printf("\t\t\tadapter_devno: %" PRIu32 "\n", devInfoData->un.scsitm.adapter_devno);
            break;
        case DD_LP:
            // do we care?
            break;
        case DD_TAPE:
            // devInfoData->un.mt
            printf("\t\tmt");
            printf("\t\t\ttype: %" PRIu8 " - ", devInfoData->un.mt.type);
            switch (devInfoData->un.mt.type)
            {
            case DT_STREAM:
                printf("Streaming tape\n");
                break;
            case DT_STRTSTP:
                printf("Start-Stop tape\n");
                break;
            default:
                printf("Unknown\n");
                break;
            }
            break;
        case DD_SCTAPE:
            // devInfoData->un.scmt
            printf("\t\tscmt");
            printf("\t\t\ttype: %" PRIu8 " - ", devInfoData->un.scmt.type);
            switch (devInfoData->un.scmt.type)
            {
            case DT_STREAM:
                printf("Streaming tape\n");
                break;
            case DT_STRTSTP:
                printf("Start-Stop tape\n");
                break;
            default:
                printf("Unknown\n");
                break;
            }
            printf("\t\t\tblksize: %" PRId32 "\n", devInfoData->un.scmt.blksize);
            break;
        case DD_DISK:
            // devInfoData->un.dk
            // dk64 //DF_LGDSK flag must be set
            if (devInfoData->flags & DF_LGDSK)
            {
                // dk64
                printf("\t\tdk64\n");
                printf("\t\t\tbytpsec = %" PRIu16 "\n", devInfoData->un.dk64.bytpsec);
                printf("\t\t\tsecptrk = %" PRIu16 "\n", devInfoData->un.dk64.secptrk);
                printf("\t\t\ttrkpcyl = %" PRIu16 "\n", devInfoData->un.dk64.trkpcyl);
                if (devInfoData->flags & DF_IVAL)
                {
                    printf("\t\t\tflags = %" PRIu16 "\n", devInfoData->un.dk64.flags);
                }
                printf("\t\t\tlo_numblks = %ld\n", devInfoData->un.dk64.lo_numblks);
                printf("\t\t\tsegment_size = %" PRIu32 "\n", devInfoData->un.dk64.segment_size);
                printf("\t\t\tsegment_count = %" PRIu32 "\n", devInfoData->un.dk64.segment_count);
                printf("\t\t\tbyte_count = %" PRIu32 "\n", devInfoData->un.dk64.byte_count);
                printf("\t\t\thi_numblks = %ld\n", devInfoData->un.dk64.hi_numblks);
            }
            else
            {
                // dk
                printf("\t\tdk\n");
                printf("\t\t\tbytpsec = %" PRIu16 "\n", devInfoData->un.dk.bytpsec);
                printf("\t\t\tsecptrk = %" PRIu16 "\n", devInfoData->un.dk.secptrk);
                printf("\t\t\ttrkpcyl = %" PRIu16 "\n", devInfoData->un.dk.trkpcyl);
                printf("\t\t\tnumblks = %ld\n", devInfoData->un.dk.numblks);
                printf("\t\t\tsegment_size = %" PRIu32 "\n", devInfoData->un.dk.segment_size);
                printf("\t\t\tsegment_count = %" PRIu32 "\n", devInfoData->un.dk.segment_count);
                printf("\t\t\tbyte_count = %" PRIu32 "\n", devInfoData->un.dk.byte_count);
            }
            break;
        case DD_CDROM:
            // case DD_SCCD:
            // devInfoData->un.sccd, idecd;
            // sccd64, idecd64 //DF_LGDSK flag must be set
            if (devInfoData->flags & DF_LGDSK)
            {
                printf("\t\tsccd64\n");
                printf("\t\t\tblksize = %" PRIu16 "\n", devInfoData->un.sccd64.blksize);
                if (devInfoData->flags & DF_IVAL)
                {
                    printf("\t\t\tflags = %" PRIu16 "\n", devInfoData->un.sccd64.flags);
                }
                printf("\t\t\tlo_numblks = %ld\n", devInfoData->un.sccd64.lo_numblks);
                printf("\t\t\thi_numblks = %ld\n", devInfoData->un.sccd64.hi_numblks);
            }
            else
            {
                printf("\t\tsccd\n");
                printf("\t\t\tblksize = %" PRIu16 "\n", devInfoData->un.sccd.blksize);
                printf("\t\t\tnumblks = %ld\n", devInfoData->un.sccd.numblks);
            }
            break;
        case DD_DLC:
            // do we care?
            break;
        case DD_SCDISK:
            /* SCSI disk, but NVMe disk if DF_NVME is set */
            // scdk, idedk;
            // devInfoData->un.scdk64, idedk64;//DF_LGDSK flag must be set
            if (devInfoData->flags & DF_LGDSK)
            {
                printf("\t\tscdk64\n");
                printf("\t\t\tblksize = %" PRIu16 "\n", devInfoData->un.scdk64.blksize);
                if (devInfoData->flags & DF_IVAL)
                {
                    printf("\t\t\tflags = %" PRIu16 "\n", devInfoData->un.scdk64.flags);
                    if (devInfoData->un.scdk64.flags & DF_SSD)
                    {
                        printf("\t\t\t\tSSD\n");
                    }
#if defined(DF_CFLASH)
                    if (devInfoData->un.scdk64.flags & DF_CFLASH)
                    {
                        printf("\t\t\t\tCAPI Flash disk\n");
                    }
#endif // DF_CFLASH
#if defined(DF_LBP)
                    if (devInfoData->un.scdk64.flags & DF_LBP)
                    {
                        printf("\t\t\t\tLBP fields are valid\n");
                    }
#endif // DF_LBP
#if defined(DF_NVME)
                    if (devInfoData->un.scdk64.flags & DF_NVME)
                    {
                        printf("\t\t\t\tNVMe\n");
                    }
#endif // DF_NVME
#if defined(DF_4B_ALIGNED)
                    if (devInfoData->un.scdk64.flags & DF_4B_ALIGNED)
                    {
                        printf("\t\t\t\t4B alignment required\n");
                    }
#endif // DF_4B_ALIGNED
#if defined(DF_NVMEM)
                    if (devInfoData->un.scdk64.flags & DF_NVMEM)
                    {
                        printf("\t\t\t\tNVMEM disk\n");
                    }
#endif // DF_NVMEM
#if defined(DF_VPMEM)
                    if (devInfoData->un.scdk64.flags & DF_VPMEM)
                    {
                        printf("\t\t\t\tContents are no persistent across CEC reboot\n");
                    }
#endif // DF_VPMEM
                }
                printf("\t\t\tlo_numblks = %ld\n", devInfoData->un.scdk64.lo_numblks);
                printf("\t\t\tlo_max_request = %ld\n", devInfoData->un.scdk64.lo_max_request);
                printf("\t\t\tsegment_size = %" PRIu32 "\n", devInfoData->un.scdk64.segment_size);
                printf("\t\t\tsegment_count = %" PRIu32 "\n", devInfoData->un.scdk64.segment_count);
                printf("\t\t\tbyte_count = %" PRIu32 "\n", devInfoData->un.scdk64.byte_count);
                printf("\t\t\thi_numblks = %ld\n", devInfoData->un.scdk64.hi_numblks);
                printf("\t\t\thi_max_request = %ld\n", devInfoData->un.scdk64.hi_max_request);
#if defined(DF_LBP)
                if (devInfoData->un.scdk64.flags & DF_LBP)
                {
                    // lbp_flags to check what other fields to print out
                    printf("\t\t\tlbp_flags = %" PRIu32 "\n", devInfoData->un.scdk64.lbp_flags);
                    if (devInfoData->un.scdk64.lbp_flags & SCDK_LBPF_ENABLED)
                    {
                        printf("\t\t\t\tLogical Block Provisioning support is enabled on this AIX node\n");
                    }
                    if (devInfoData->un.scdk64.lbp_flags & SCDK_LBPF_UNSUPPORTED_DEVICE)
                    {
                        printf("\t\t\t\tAIX does not support Logical Block Provisioning on this device\n");
                    }
                    if (devInfoData->un.scdk64.lbp_flags & SCDK_LBPF_ALIGN_FIELD_VALID)
                    {
                        printf("\t\t\t\tlbp_alignment is valid\n");
                    }
                    if (devInfoData->un.scdk64.lbp_flags & SCDK_LBPF_READ_ZEROS)
                    {
                        printf("\t\t\t\tUnmapped blocks return zero on read\n");
                    }
                    if (devInfoData->un.scdk64.lbp_flags & SCDK_LBPF_WS_SUPPORTED)
                    {
                        printf("\t\t\t\tWrite Same command is supported\n");
                    }
                    if (devInfoData->un.scdk64.lbp_flags & SCDK_LBPF_UNMAP_SUPPORTED)
                    {
                        printf("\t\t\t\tUnmap command is supported\n");
                    }
                    if (devInfoData->un.scdk64.lbp_flags & SCDK_LBPF_CANCEL_SUPPORTED)
                    {
                        printf("\t\t\t\tDriver supports ioctl(DK_CANCEL_RECLAIM)\n");
                    }
                    printf("\t\t\tlbp_provision_type = %" PRIu8 " - ", devInfoData->un.scdk64.lbp_provision_type);
                    switch (devInfoData->un.scdk64.lbp_provision_type)
                    {
                    case LBP_TYPE_THICK:
                        printf("Thick\n");
                        break;
                    case LBP_TYPE_THIN:
                        printf("Thin\n");
                        break;
                    case LBP_TYPE_UNKNOWN:
                    default:
                        printf("Unknown\n");
                        break;
                    }
                    printf("\t\t\tlbp_max_blks = %" PRIu32 "\n", devInfoData->un.scdk64.lbp_max_blks);
                    printf("\t\t\tlbp_optimal_blks = %" PRIu32 "\n", devInfoData->un.scdk64.lbp_optimal_blks);
                    if (devInfoData->un.scdk64.lbp_flags & SCDK_LBPF_ALIGN_FIELD_VALID)
                    {
                        printf("\t\t\tlbp_alignment = %" PRIu32 "\n", devInfoData->un.scdk64.lbp_alignment);
                    }
                }
#endif // DF_LBP
            }
            else
            {
                printf("\t\tscdk\n");
                printf("\t\t\tblksize = %" PRIu16 "\n", devInfoData->un.scdk.blksize);
                printf("\t\t\tnumblks = %ld\n", devInfoData->un.scdk.numblks);
                printf("\t\t\tmax_request = %ld\n", devInfoData->un.scdk.max_request);
                printf("\t\t\tsegment_size = %" PRIu32 "\n", devInfoData->un.scdk.segment_size);
                printf("\t\t\tsegment_count = %" PRIu32 "\n", devInfoData->un.scdk.segment_count);
                printf("\t\t\tbyte_count = %" PRIu32 "\n", devInfoData->un.scdk.byte_count);
            }
            break;
        case DD_RTC:
            // do we care?
            break;
        case DD_PSEU:
            // do we care?
            break;
        case DD_NET:
            // do we care?
            break;
        case DD_EN:
            // do we care?
            break;
        case DD_EM78:
            // do we care?
            break;
        case DD_TR:
            // do we care?
            break;
        case DD_BIO:
            // do we care?
            break;
        case DD_X25:
            // do we care?
            break;
        case DD_IEEE_3:
            // do we care?
            break;
        case DD_SL:
            // do we care?
            break;
        case DD_LO:
            // do we care?
            break;
        case DD_DUMP:
            // do we care?
            break;
        case DD_CIO:
            // do we care?
            break;
        case DD_DISK_C: // disk controller - fallthrough??? not sure if this goes here or somewhere else
        case DD_BUS:
            // adapters for SCSI controllers will show up with this
            // https://www.ibm.com/docs/en/aix/7.1?topic=drivers-sam-adapter-ioctl-operations
            // check the subtype for specific info
            switch (devInfoData->devsubtype)
            {
            case DS_SCSI: /* SCSI adapter */
                printf("\t\tscsi\n");
                printf("\t\t\tcard_scsi_id = %" PRId8 "\n", devInfoData->un.scsi.card_scsi_id);
                printf("\t\t\tmax_transfer = %ld\n", devInfoData->un.scsi.max_transfer);
                break;
            case DS_IDE: /* IDE adapter  */
                printf("\t\tide\n");
                printf("\t\t\tresv1 = %" PRId8 "\n", devInfoData->un.ide.resv1);
                printf("\t\t\tmax_transfer = %ld\n", devInfoData->un.ide.max_transfer);
                break;
            case DS_SAS: /* SAS adapter  */
                printf("\t\tsas\n");
                if (devInfoData->flags & DF_IVAL)
                {
                    printf("\t\t\tflags = %" PRIu8 "\n", devInfoData->un.sas.flags);
                    if (devInfoData->un.sas.flags & SAS_FLAGS_DK_BUFX_EXT)
                    {
                        printf("\t\t\t\tDriver supports disk_bufx_ext\n");
                    }
                }
                printf("\t\t\treserved1 = %" PRIu8 "\n", devInfoData->un.sas.reserved1);
                printf("\t\t\treserved2 = %" PRIu16 "\n", devInfoData->un.sas.reserved2);
                printf("\t\t\treserved3 = %" PRIu32 "\n", devInfoData->un.sas.reserved3);
                printf("\t\t\treserved4 = %" PRIu32 "\n", devInfoData->un.sas.reserved4);
                printf("\t\t\treserved5 = %" PRIu32 "\n", devInfoData->un.sas.reserved5);
                printf("\t\t\tmax_transfer = %" PRId32 "\n", devInfoData->un.sas.max_transfer);
                break;
            case DS_SATA: /* SATA adapter */
                printf("\t\tsata\n");
                if (devInfoData->flags & DF_IVAL)
                {
                    printf("\t\t\tflags = %" PRIu8 "\n", devInfoData->un.sata.flags);
                }
                printf("\t\t\treserved1 = %" PRIu8 "\n", devInfoData->un.sata.reserved1);
                printf("\t\t\treserved2 = %" PRIu16 "\n", devInfoData->un.sata.reserved2);
                printf("\t\t\treserved3 = %" PRIu32 "\n", devInfoData->un.sata.reserved3);
                printf("\t\t\treserved4 = %" PRIu32 "\n", devInfoData->un.sata.reserved4);
                printf("\t\t\treserved5 = %" PRIu32 "\n", devInfoData->un.sata.reserved5);
                printf("\t\t\tmax_transfer = %" PRId32 "\n", devInfoData->un.sata.max_transfer);
                break;
            case DS_SDA: /* Serial DASD adapter */
                // http://ps-2.kev009.com/tl/techlib/manuals/adoclib/libs/ktechrf2/serialda.htm
                //???
                break;
            case DS_SDC: /* Serial DASD Controller */
                //???
                break;
            case DS_FCP: /* FC SCSI adapter        */
                break;
#if !defined(DISABLE_NVME_PASSTHROUGH)
            case DS_FCNVME: /* FC-NVMe device       */
                break;
#endif                     // DISABLE_NVME_PASSTHROUGH
            case DS_ISCSI: /* iSCSI adapter           */
                break;
#if defined(DS_CAPI_IO)
            case DS_CAPI_IO: /* CAPI Storage device */
                break;
#endif // DS_CAPI_IO
#if !defined(DISABLE_NVME_PASSTHROUGH)
            case DS_NVME: /* non-volatile Memory controller   */
                printf("\t\tnvme\n");
                printf("\t\t\treserved1 = %" PRIu32 "\n", devInfoData->un.nvme.reserved1);
                printf("\t\t\tcapability = %" PRIu32 "\n", devInfoData->un.nvme.capability);
                if (devInfoData->un.nvme.capability == 0)
                {
                    printf("\t\t\t\tAdapter is using PRP & 4B data address alignment for r/w\n")
                }
                else if (devInfoData->un.nvme.capability == NVME_CAP_SGL)
                {
                    printf("\t\t\t\tAdapter is using SGL for r/w\n")
                }
                printf("\t\t\tmajor_version = %" PRIu16 "\n", devInfoData->un.nvme.major_version);
                printf("\t\t\tminor_version = %" PRIu8 "\n", devInfoData->un.nvme.minor_version);
                if (devInfoData->flags & DF_IVAL)
                {
                    printf("\t\t\tflags = %" PRIu8 "\n", devInfoData->un.nvme.flags);
                    if (devInfoData->un.nvme.flags & NVME_STATIC_CTLR)
                    {
                        printf("\t\t\t\tStatic Controller\n");
                    }
                    if (devInfoData->un.nvme.flags & NVME_DYNAMIC_CTLR)
                    {
                        printf("\t\t\t\tDynamic Controller\n");
                    }
                    if (devInfoData->un.nvme.flags & NVME_DISCOVERY_CTLR)
                    {
                        printf("\t\t\t\tDiscovery Controller\n");
                    }
                    if (devInfoData->un.nvme.flags & NVME_REMOTE_CTLR)
                    {
                        printf("\t\t\t\tRemote Controller\n");
                    }
                }
                printf("\t\t\tmax_transfer = %" PRId32 "\n", devInfoData->un.nvme.max_transfer);
                printf("\t\t\tioctl_max_transfer = %" PRId32 "\n", devInfoData->un.nvme.ioctl_max_transfer);
                break;
#endif                      // DISABLE_NVME_PASSTHROUGH
            case DS_CVSCSI: /* Virtual SCSI Client (hosteD)  */
                break;
            case DS_SVSCSI: /* Virtual SCSI Server (hostinG) */
                break;
#if defined(DS_VRTSCSI)
            case DS_VRTSCSI: /* VirtIO SCSI Client Adapter */
                printf("\t\tvrt_scsi\n");
                printf("\t\t\tvrtscsi_id = %" PRId8 "\n", devInfoData->un.vrt_scsi.vrtscsi_id);
                printf("\t\t\tmax_transfer = %" PRId32 "\n", devInfoData->un.vrt_scsi.max_transfer);
                printf("\t\t\treserved1 = %" PRId32 "\n", devInfoData->un.vrt_scsi.reserved1);
                printf("\t\t\treserved2 = %" PRId32 "\n", devInfoData->un.vrt_scsi.reserved2);
                printf("\t\t\treserved3 = %" PRId32 "\n", devInfoData->un.vrt_scsi.reserved3);
                break;
#endif // DS_VRTSCSI
            default:
                // Do nothing for now. We can parse more things out when we need to in the future -TJE
                break;
            }
            break;
        case DD_HFT:
            // do we care?
            break;
        case DD_INPUT:
            // do we care?
            break;
        case DD_CON:
            // do we care?
            break;
        case DD_NET_DH:
            // do we care?
            break;
        case DD_SOL:
            // do we care?
            break;
        case DD_CAT:
            // do we care?
            break;
        case DD_FDDI:
            // do we care?
            break;
        case DD_SCRWOPT:
            // same as SCSI disk or cdrom???
            break;
        case DD_SES:
            // same as SCSI disk or cdrom???
            break;
        case DD_AUDIT:
            // do we care?
            break;
        case DD_LIB:
            // do we care?
            break;
        case DD_VIOA:
            // do we care?
            break;
        case DD_OTHER:
            // do we care?
            break;
        default:
            // do we care?
            break;
        }
    }
}

static void print_ODM_Error(int odmError)
{
    switch (odmError)
    {
    case ODMI_OPEN_ERR:
        printf("ODMI Cannot open object class\n");
        break;
    case ODMI_MALLOC_ERR:
        printf("ODMI Cannot allocate memory\n");
        break;
    case ODMI_MAGICNO_ERR:
        printf("ODMI Invalid file magic number\n");
        break;
    case ODMI_NO_OBJECT:
        printf("ODMI no object\n");
        break;
    case ODMI_BAD_CRIT:
        printf("ODMI Invalid search criteria\n");
        break;
    case ODMI_INTERNAL_ERR:
        printf("ODMI Internal Error\n");
        break;
    case ODMI_TOOMANYCLASSES:
        printf("ODMI Accessing too many classes\n");
        break;
    case ODMI_LINK_NOT_FOUND:
        printf("ODMI Link not found\n");
        break;
    case ODMI_INVALID_CLASS:
        printf("ODMI invalid class\n");
        break;
    case ODMI_CLASS_EXISTS:
        printf("ODMI class exists\n");
        break;
    case ODMI_CLASS_DNE:
        printf("ODMI class dne\n"); // DNE = does not exist???
        break;
    case ODMI_BAD_CLASSNAME:
        printf("ODMI bad classname\n");
        break;
    case ODMI_UNLINKCLASS_ERR:
        printf("ODMI unlinkclass error\n");
        break;
    case ODMI_UNLINKCLXN_ERR:
        printf("ODMI unlink clxn error\n");
        break;
    case ODMI_INVALID_CLXN:
        printf("ODMI invalid clxn\n");
        break;
    case ODMI_CLXNMAGICNO_ERR:
        printf("ODMI clxn magic number error\n");
        break;
    case ODMI_BAD_CLXNNAME:
        printf("ODMI bad clxn name\n");
        break;
    case ODMI_CLASS_PERMS:
        printf("ODMI PERMISSIONS DON'T ALLOW OPEN\n");
        break;
    case ODMI_BAD_TIMEOUT:
        printf("ODMI INVALID TIMEOUT VALUE\n");
        break;
    case ODMI_BAD_TOKEN:
        printf("ODMI UNABLE TO OPEN/CREATE TOKEN\n");
        break;
    case ODMI_LOCK_BLOCKED:
        printf("ODMI ANOTHER PROCESS HAS LOCK\n");
        break;
    case ODMI_LOCK_ENV:
        printf("ODMI CANNOT GET/SET ENV VARIABLE\n");
        break;
    case ODMI_UNLOCK:
        printf("ODMI CANNOT UNLOCK THE TOKEN\n");
        break;
    case ODMI_BAD_LOCK:
        printf("ODMI UNABLE TO SET LOCK\n");
        break;
    case ODMI_LOCK_ID:
        printf("ODMI INVALID LOCK ID\n");
        break;
    case ODMI_PARAMS:
        printf("ODMI INVALID PARAMETERS PASSED IN\n");
        break;
    case ODMI_OPEN_PIPE:
        printf("ODMI COULD NOT OPEN CHILD PIPE\n");
        break;
    case ODMI_READ_PIPE:
        printf("ODMI COULD NOT READ FROM CHILD PIPE\n");
        break;
    case ODMI_FORK:
        printf("ODMI COULD NOT FORK CHILD PROCESS\n");
        break;
    case ODMI_INVALID_PATH:
        printf("ODMI PATH OR FILE IS INVALID\n");
        break;
    case ODMI_READ_ONLY:
        printf("ODMI CLASS IS OPENED AS READ-ONLY\n");
        break;
    case ODMI_NO_SPACE:
        printf("ODMI FILESYSTEM FULL\n");
        break;
    case ODMI_VERSION_ERROR:
        printf("ODMI Invalid object class version\n");
        break;
    default:
        printf("ODMI Unknown error: %d\n", odmError);
        break;
    }
}

static void print_CuDv_Struct(struct CuDv* cudv)
{
    // making copies to ensure M_NULLPTR termination -TJE
    DECLARE_ZERO_INIT_ARRAY(char, cudvName, 17);
    DECLARE_ZERO_INIT_ARRAY(char, cudvddins, 17);
    DECLARE_ZERO_INIT_ARRAY(char, cudvlocation, 17);
    DECLARE_ZERO_INIT_ARRAY(char, cudvparent, 17);
    DECLARE_ZERO_INIT_ARRAY(char, cudvconnwhere, 17);
    DECLARE_ZERO_INIT_ARRAY(char, cudvPdDvLnLvalue, 49);
    snprintf_err_handle(cudvName, 17, "%s", cudv->name);
    snprintf_err_handle(cudvddins, 17, "%s", cudv->ddins);
    snprintf_err_handle(cudvlocation, 17, "%s", cudv->location);
    snprintf_err_handle(cudvparent, 17, "%s", cudv->parent);
    snprintf_err_handle(cudvconnwhere, 17, "%s", cudv->connwhere);
    snprintf_err_handle(cudvPdDvLnLvalue, 49, "%s", cudv->PdDvLn_Lvalue);
    printf("CuDv:\n");
    printf("\tid: %ld\n", cudv->_id);
    printf("\treserved: %ld\n", cudv->_reserved);
    printf("\tscratch: %ld\n", cudv->_scratch);
    printf("\tname: %s\n", cudvName);
    printf("\tstatus: %" PRId16 "\n", cudv->status);
    printf("\tchgstatus: %" PRId16 "\n", cudv->chgstatus);
    printf("\tddins: %s\n", cudvddins);
    printf("\tlocation: %s\n", cudvlocation);
    printf("\tparent: %s\n", cudvparent);
    printf("\tconnwhere: %s\n", cudvconnwhere);
    if (cudv->PdDvLn)
    {
        DECLARE_ZERO_INIT_ARRAY(char, pddvtype, 17);
        DECLARE_ZERO_INIT_ARRAY(char, pddvclass, 17);
        DECLARE_ZERO_INIT_ARRAY(char, pddvsubclass, 17);
        DECLARE_ZERO_INIT_ARRAY(char, pddvprefix, 17);
        DECLARE_ZERO_INIT_ARRAY(char, pddvdevid, 17);
        DECLARE_ZERO_INIT_ARRAY(char, pddvcatalog, 17);
        DECLARE_ZERO_INIT_ARRAY(char, pddvDvDr, 17);
        DECLARE_ZERO_INIT_ARRAY(char, pddvDefine, 257);
        DECLARE_ZERO_INIT_ARRAY(char, pddvConfigure, 257);
        DECLARE_ZERO_INIT_ARRAY(char, pddvChange, 257);
        DECLARE_ZERO_INIT_ARRAY(char, pddvUnconfigure, 257);
        DECLARE_ZERO_INIT_ARRAY(char, pddvUndefine, 257);
        DECLARE_ZERO_INIT_ARRAY(char, pddvStart, 257);
        DECLARE_ZERO_INIT_ARRAY(char, pddvStop, 257);
        DECLARE_ZERO_INIT_ARRAY(char, pddvuniquetype, 49);
        // making copies to ensure null termination - TJE
        snprintf_err_handle(pddvtype, 17, "%s", cudv->PdDvLn->type);
        snprintf_err_handle(pddvclass, 17, "%s", cudv->PdDvLn->class);
        snprintf_err_handle(pddvsubclass, 17, "%s", cudv->PdDvLn->subclass);
        snprintf_err_handle(pddvprefix, 17, "%s", cudv->PdDvLn->prefix);
        snprintf_err_handle(pddvdevid, 17, "%s", cudv->PdDvLn->devid);
        snprintf_err_handle(pddvcatalog, 17, "%s", cudv->PdDvLn->catalog);
        snprintf_err_handle(pddvDvDr, 17, "%s", cudv->PdDvLn->DvDr);
        snprintf_err_handle(pddvDefine, 257, "%s", cudv->PdDvLn->Define);
        snprintf_err_handle(pddvConfigure, 257, "%s", cudv->PdDvLn->Configure);
        snprintf_err_handle(pddvChange, 257, "%s", cudv->PdDvLn->Change);
        snprintf_err_handle(pddvUnconfigure, 257, "%s", cudv->PdDvLn->Unconfigure);
        snprintf_err_handle(pddvUndefine, 257, "%s", cudv->PdDvLn->Undefine);
        snprintf_err_handle(pddvStart, 257, "%s", cudv->PdDvLn->Start);
        snprintf_err_handle(pddvStop, 257, "%s", cudv->PdDvLn->Stop);
        snprintf_err_handle(pddvuniquetype, 49, "%s", cudv->PdDvLn->uniquetype);
        printf("\tPdDv\n");
        printf("\t\tid: %ld\n", cudv->PdDvLn->_id);
        printf("\t\treserved: %ld\n", cudv->PdDvLn->_reserved);
        printf("\t\tscratch: %ld\n", cudv->PdDvLn->_scratch);
        printf("\t\ttype: %s\n", pddvtype);
        printf("\t\tclass: %s\n", pddvclass);
        printf("\t\tsubclass: %s\n", pddvsubclass);
        printf("\t\tprefix: %s\n", pddvprefix);
        printf("\t\tdevid: %s\n", pddvdevid);
        printf("\t\tbase: %" PRId16 "\n", cudv->PdDvLn->base);
        printf("\t\thas_vpd: %" PRId16 "\n", cudv->PdDvLn->has_vpd);
        printf("\t\tdetectable: %" PRId16 "\n", cudv->PdDvLn->detectable);
        printf("\t\tchgstatus: %" PRId16 "\n", cudv->PdDvLn->chgstatus);
        printf("\t\tbus_ext: %" PRId16 "\n", cudv->PdDvLn->bus_ext);
        printf("\t\tfru: %" PRId16 "\n", cudv->PdDvLn->fru);
        printf("\t\tled: %" PRId16 "\n", cudv->PdDvLn->led);
        printf("\t\tsetno: %" PRId16 "\n", cudv->PdDvLn->setno);
        printf("\t\tmsgno: %" PRId16 "\n", cudv->PdDvLn->msgno);
        printf("\t\tcatalog: %s\n", pddvcatalog);
        printf("\t\tDvDr: %s\n", pddvDvDr);
        printf("\t\tDefine: %s\n", pddvDefine);
        printf("\t\tConfigure: %s\n", pddvConfigure);
        printf("\t\tChange: %s\n", pddvChange);
        printf("\t\tUnconfigure: %s\n", pddvUnconfigure);
        printf("\t\tUndefine: %s\n", pddvUndefine);
        printf("\t\tStart: %s\n", pddvStart);
        printf("\t\tStop: %s\n", pddvStop);
        printf("\t\tinventory_only: %" PRId16 "\n", cudv->PdDvLn->inventory_only);
        printf("\t\tuniquetype: %s\n", pddvuniquetype);
    }
    if (cudv->PdDvLn_info)
    {
        // making copies to ensure null termination -TJE
        DECLARE_ZERO_INIT_ARRAY(char, listinfoClassname, MAX_ODMI_NAME + 1);
        DECLARE_ZERO_INIT_ARRAY(char, listinfoCrit, MAX_ODMI_CRIT + 1);
        snprintf_err_handle(listinfoClassname, MAX_ODMI_NAME + 1, "%s", cudv->PdDvLn_info->classname);
        snprintf_err_handle(listinfoCrit, MAX_ODMI_CRIT + 1, "%s", cudv->PdDvLn_info->crit);
        printf("\t\tlistinfo:\n");
        printf("\t\t\tclassname: %s\n", listinfoClassname);
        printf("\t\t\tcrit: %s\n", listinfoCrit);
        printf("\t\t\tnum: %d\n", cudv->PdDvLn_info->num);
        printf("\t\t\tnum: %d\n", cudv->PdDvLn_info->valid);
        // TODO: Print out this structure...this seems to get deeper and deeper that I'm stopping here for now - TJE
        //  if (cudv->PdDvLn_info->class)//note: In C++ this is named ____class
        //  {
        //      //print out this structure
        //      // struct Class {
        //      //     int begin_magic;
        //      //     char *classname;
        //      //     int structsize;
        //      //     int nelem;
        //      //     struct ClassElem *elem;
        //      //     struct StringClxn *clxnp;
        //      //     int open;
        //      //     struct ClassHdr  *hdr;
        //      //     char *data;
        //      //     int fd;
        //      //     int current;
        //      //     struct Crit *crit;
        //      //     int ncrit;
        //      //     char critstring[MAX_ODMI_CRIT];
        //      //     int reserved;
        //      //     int end_magic;
        //      // };
        //  }
    }
    printf("\tPdDvLn_Lvalue: %s\n", cudvPdDvLnLvalue);
}

static int get_Adapter_IDs(tDevice* device, char* name)
{
    int          ret = 0;
    struct CuDv  cudv;
    struct CuDv* ptrcudv;
    safe_memset(&cudv, sizeof(struct CuDv), 0, sizeof(struct CuDv));

    // odm_initialize();
    DECLARE_ZERO_INIT_ARRAY(char, odmCriteria, MAX_ODMI_CRIT); // 256
    if (name && safe_strlen(name) > 0)
    {
        snprintf_err_handle(odmCriteria, MAX_ODMI_CRIT, "name='%s'", name);
        ptrcudv = odm_get_obj(CuDv_CLASS, odmCriteria, &cudv, ODM_FIRST);
        if (ptrcudv != M_NULLPTR)
        {
            // the parent should be available in ptrcudv now.
            if (device->deviceVerbosity > VERBOSITY_DEFAULT)
            {
                print_CuDv_Struct(ptrcudv);
            }

            // first check if PdDvLn = "adapter/pci... in it. If it does, then this is the result we are looking for
            if (strstr(ptrcudv->PdDvLn_Lvalue, "adapter/pci"))
            {
                // Through trial and error testing and searching, this is a typical output we want to find and parse:
                // adapter/pci/pcividpid
                // or
                // adapter/pciex/pcividpid
                // Note that the vid and pid are not in the correct order. These are reported as little endian,
                // but the big endian AIX system does not seem to swap them to the correct order for it...looks like
                // it just copies them which leaves them in the wrong order.

                // Set a char pointer to the last / + 1
                const char* ids = strrchr(ptrcudv->PdDvLn_Lvalue, '/') + 1;
                // now convert this out to a uint32, then byte swap it, then separate into VID and PID
                unsigned long idCombo = 0UL;
                if (0 != safe_strtoul(&idCombo, ids, &endptr, BASE_16_HEX))
                {
                    // unable to convert the string for some reason
                    ret = -1;
                }
                else
                {
#if defined(ENV_BIG_ENDIAN)
                    // wrapping this as if there was little endian AIX, I doubt it would exhibit the same issue
                    byte_Swap_32(&idCombo);
                    word_Swap_32(&idCombo); // This is done after the byte swap since it will change the order of VID
                                            // and PID back to expected place
#endif                                      // BIG ENDIAN check
                    device->drive_info.adapter_info.infoType       = ADAPTER_INFO_PCI;
                    device->drive_info.adapter_info.vendorID       = M_Word1(idCombo);
                    device->drive_info.adapter_info.productID      = M_Word0(idCombo);
                    device->drive_info.adapter_info.vendorIDValid  = true;
                    device->drive_info.adapter_info.productIDValid = true;
                    ret                                            = 1;
                }
            }
            else
            {
                // Check the parent. from hdisk, this is usually 2 parents up from what I've seen -TJE
                ret = get_Adapter_IDs(device, ptrcudv->parent);
            }
        }
        else
        {
            ret = -1; // some kind of error
        }
    }
    else
    {
        ret = -1;
    }
    return ret;
}

// While we are unlikely to see many, if any, USB devices in AIX,
// it is possible to read the vendor/product IDs somewhere in the attributes:
// https://www.ibm.com/docs/en/aix/7.3?topic=subsystem-usblibdd-passthru-driver
eReturnValues get_Device(const char* filename, tDevice* device)
{
    // use openx. Do not set the SC_DIAGNOSTIC. That can be redone in the lock/unlock routines.
    // open can be used but always performed a SCSI2 reserve. Not necessary for this software
    // first open the provided handle
    // Possible Extension flags:
    // SC_DIAGNOSTIC - more or less exclusive. Required for certain dianostic mode IOCTLs
    // SC_FORCED_OPEN_LUN - lun reset or target reset will occur regardless of any reservations
    // SC_FORCED_OPEN - forces a bus reset before opening
    // SC_RETAIN_RESERVATION - retains reservation of device after close by not issuing the release (Might be useful if
    // using the IOCTLs for reservations - TJE) SC_NO_RESERVE - prevents reservation during openx (this might be good
    // for us since we are not usually reading or writing -TJE) SC_SINGLE - places device in exclusive access mode.
    // (Reservation exclusive access mode???) SC_PR_SHARED_REGISTER - persistent reserve, register and ignore key is
    // used when opening
    eReturnValues ret            = SUCCESS;
    long          extensionFlags = 0L;
    bool          handleOpened   = false;
    if (device->deviceVerbosity > VERBOSITY_DEFAULT)
    {
        printf("\nAIX attempting to open device: %s\n", filename);
    }
    if ((device->os_info.fd = openx(C_CAST(char*, filename), 0, 0, extensionFlags)) >= 0) // path, OFlag, Mode,
                                                                                          // Extension
    {
        handleOpened = true;
    }
    else // retry with the diadnostic flag set
    {
        extensionFlags = SC_DIAGNOSTIC;
        if ((device->os_info.fd = openx(C_CAST(char*, filename), 0, 0, extensionFlags)) >=
            0) // path, OFlag, Mode, Extension
        {
            handleOpened = true;
        }
        else
        {
            ret = FAILURE;
        }
    }
    if (handleOpened)
    {
        // able to open the device. Read the devinfo, then open controller and read its devinfo -TJE
        struct devinfo driveInfo;
        safe_memset(&driveInfo, sizeof(struct devinfo), 0, sizeof(struct devinfo));
        if (device->deviceVerbosity > VERBOSITY_DEFAULT)
        {
            printf("Attempting device IOCINFO\n");
        }
        if (extensionFlags & SC_DIAGNOSTIC)
        {
            device->os_info.diagnosticModeFlagInUse = true;
        }
        if (ioctl(device->os_info.fd, IOCINFO, &driveInfo) >= 0)
        {
            // Got the devinfo, now parse the data into something we can use for later
            // TODO: Filter out invalid device types we do not support.-TJE
            if (device->deviceVerbosity > VERBOSITY_DEFAULT)
            {
                print_devinfo_struct(&driveInfo);
            }
        }
        else
        {
            device->os_info.last_error = errno;
            if (device->deviceVerbosity > VERBOSITY_COMMAND_NAMES)
            {
                printf("Device IOCINFO Error: ");
                print_Errno_To_Screen(device->os_info.last_error);
            }
            if (device->os_info.last_error == EACCES)
            {
                ret = PERMISSION_DENIED;
            }
            else
            {
                ret = FAILURE;
            }
        }
        device->os_info.minimumAlignment = sizeof(void*); // for now use this. There are some devices that require 4B
                                                          // alignment, but this will most likely take care of that -TJE
        // Now get the parent handle, open it and request the IOCINFO for the parent since that fill provide more
        // details -TJE set name and friendly name
        snprintf_err_handle(device->os_info.name, OS_HANDLE_NAME_MAX_LENGTH, "%s", filename);
        char*   friendlyName = M_NULLPTR;
        errno_t duperr       = safe_strdup(&friendlyName, filename);
        if (duperr == 0 && friendlyName != M_NULLPTR)
        {
            snprintf_err_handle(device->os_info.friendlyName, OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH, "%s",
                                basename(friendlyName));
        }
        safe_free(&friendlyName);
        struct CuDv  cudv;
        struct CuDv* ptrcudv;
        safe_memset(&cudv, sizeof(struct CuDv), 0, sizeof(struct CuDv));

        odm_initialize();
        DECLARE_ZERO_INIT_ARRAY(char, odmCriteria, MAX_ODMI_CRIT); // 256
        char* diskFullName = M_NULLPTR;
        duperr             = safe_strdup(&diskFullName, filename);
        if (duperr != 0 || diskFullName == M_NULLPTR)
        {
            return MEMORY_FAILURE;
        }
        char* diskName = strrchr(diskFullName, 'r'); // point to r in /dev/rhdisk#
        if (diskName != M_NULLPTR)
        {
            diskName++; // point just past the r so that it is only hdisk#
            snprintf_err_handle(odmCriteria, MAX_ODMI_CRIT, "name='%s'", diskName);
            ptrcudv = odm_get_obj(CuDv_CLASS, odmCriteria, &cudv, ODM_FIRST);
            if (ptrcudv != M_NULLPTR)
            {
                // the parent should be available in ptrcudv now.
                if (device->deviceVerbosity > VERBOSITY_DEFAULT)
                {
                    print_CuDv_Struct(ptrcudv);
                }
                if (safe_strlen(ptrcudv->parent) > 0)
                {
                    // open the controller handle and get the IOCINFO for it -TJE
                    DECLARE_ZERO_INIT_ARRAY(char, controllerHandle, OS_HANDLE_NAME_MAX_LENGTH);
                    snprintf_err_handle(controllerHandle, OS_HANDLE_NAME_MAX_LENGTH, "/dev/%s\n", ptrcudv->parent);

                    if (device->deviceVerbosity > VERBOSITY_DEFAULT)
                    {
                        printf("\nAIX attempting to open controller: %s\n", controllerHandle);
                    }
                    if ((device->os_info.ctrlfd = openx(controllerHandle, 0, 0, 0)))
                    {
                        // successfully opened the controller's handle
                        device->os_info.ctrlfdValid = true;
                        // note: IOCINFO does not seem to work on controllers.
                    }
                    else
                    {
                        if (device->deviceVerbosity > VERBOSITY_DEFAULT)
                        {
                            printf("Unable to open controller handle: %s - ", ptrcudv->parent);
                            print_Errno_To_Screen(errno);
                        }
                    }
                    // based off the name of the controller, set up the interface info.
                    // NOLINTBEGIN(bugprone-branch-clone)
                    if (strstr(ptrcudv->parent, "sata"))
                    {
                        // set up SATA passthrough
                        device->os_info.adapterType = AIX_ADAPTER_SATA;
                        device->os_info.ptType = AIX_PASSTHROUGH_SATA; // If we ever get a handle other than rhdisk,
                                                                       // switch to atapi or SCSI for those handles-TJE
                        device->drive_info.drive_type     = ATA_DRIVE;
                        device->drive_info.interface_type = IDE_INTERFACE;
                    }
                    else if (strstr(ptrcudv->parent, "ide"))
                    {
                        // setup IDE passthrough
                        device->os_info.adapterType = AIX_ADAPTER_IDE;
                        device->os_info.ptType = AIX_PASSTHROUGH_IDE_ATA; // If we ever get a handle other than rhdisk,
                                                                          // switch to atapi for those handles-TJE
                        device->drive_info.drive_type     = ATA_DRIVE;
                        device->drive_info.interface_type = IDE_INTERFACE;
                    }
                    else if (strstr(ptrcudv->parent, "fscsi")) // fibre channel
                    {
                        device->os_info.adapterType       = AIX_ADAPTER_FC;
                        device->os_info.ptType            = AIX_PASSTHROUGH_SCSI;
                        device->drive_info.drive_type     = SCSI_DRIVE;
                        device->drive_info.interface_type = SCSI_INTERFACE;
                    }
                    else if (strstr(ptrcudv->parent, "vscsi")) // virtual scsi?
                    {
                        device->os_info.adapterType       = AIX_ADAPTER_VSCSI;
                        device->os_info.ptType            = AIX_PASSTHROUGH_SCSI;
                        device->drive_info.drive_type     = SCSI_DRIVE;
                        device->drive_info.interface_type = SCSI_INTERFACE;
                    }
                    else if (strstr(ptrcudv->parent, "iscsi")) // iSCSI
                    {
                        device->os_info.adapterType       = AIX_ADAPTER_ISCSI;
                        device->os_info.ptType            = AIX_PASSTHROUGH_SCSI;
                        device->drive_info.drive_type     = SCSI_DRIVE;
                        device->drive_info.interface_type = SCSI_INTERFACE;
                    }
                    else if (strstr(ptrcudv->parent, "scsi")) // note this is parallel scsi
                    {
                        // SCSI passthrough.
                        device->os_info.adapterType       = AIX_ADAPTER_SCSI;
                        device->os_info.ptType            = AIX_PASSTHROUGH_SCSI;
                        device->drive_info.drive_type     = SCSI_DRIVE;
                        device->drive_info.interface_type = SCSI_INTERFACE;
                    }
                    else if (strstr(ptrcudv->parent, "sas"))
                    {
                        device->os_info.adapterType       = AIX_ADAPTER_SAS;
                        device->os_info.ptType            = AIX_PASSTHROUGH_SCSI;
                        device->drive_info.drive_type     = SCSI_DRIVE;
                        device->drive_info.interface_type = SCSI_INTERFACE;
                    }
                    else if (strstr(ptrcudv->parent, "nvme"))
                    {
                        device->os_info.adapterType       = AIX_ADAPTER_NVME;
                        device->os_info.ptType            = AIX_PASSTHROUGH_NVME;
                        device->drive_info.drive_type     = NVME_DRIVE;
                        device->drive_info.interface_type = NVME_INTERFACE;
                    }
                    else if (strstr(ptrcudv->parent, "serdasd"))
                    {
                        device->os_info.adapterType       = AIX_ADAPTER_DASD;
                        device->os_info.ptType            = AIX_PASSTHROUGH_SCSI;
                        device->drive_info.drive_type     = SCSI_DRIVE;
                        device->drive_info.interface_type = USB_INTERFACE;
                    }
                    else if (strstr(ptrcudv->parent, "usb"))
                    {
                        device->os_info.adapterType       = AIX_ADAPTER_USB;
                        device->os_info.ptType            = AIX_PASSTHROUGH_SCSI;
                        device->drive_info.drive_type     = SCSI_DRIVE;
                        device->drive_info.interface_type = USB_INTERFACE;
                    }
                    // NOLINTEND(bugprone-branch-clone)
                    else
                    {
                        // assume SCSI???
                        // or try a bunch until it works?
                    }

                    //                         if (driveInfo.devtype == DD_SCDISK)
                    //                         {
                    //                             if (driveInfo.flags & DF_LGDSK && driveInfo.flags & DF_IVAL)
                    //                             {
                    //                                 //check if NVMe
                    // #if defined (DF_NVME) && !defined (DISABLE_NVME_PASSTHROUGH)
                    //                                 if (driveInfo.un.scdk64.flags & DF_NVME)
                    //                                 {
                    //                                     //NVMe device & interface. NOTE: need to make sure we were
                    //                                     able to successfully
                    //                                     //able to open the controller handle before we enable the
                    //                                     following code or
                    //                                     //unexpected behavior will likely occur. -TJE
                    //                                     if (device->os_info.ctrlfdValid)
                    //                                     {
                    //                                         device->drive_info.drive_type = NVME_DRIVE;
                    //                                         device->drive_info.interface_type = NVME_INTERFACE;
                    //                                     }
                    //                                     else
                    //                                     {
                    //                                         device->drive_info.drive_type = SCSI_DRIVE;
                    //                                         device->drive_info.interface_type = SCSI_INTERFACE;
                    //                                     }
                    //                                 }
                    //                                 else //not NVMe
                    // #endif //DF_NVME && DISABLE_NVME_PASSTHROUGH
                    //                                 {
                    //                                     if (device->os_info.ctrlfdValid)
                    //                                     {
                    //                                         //set additional flags???
                    //                                     }
                    //                                 }
                    //                             }
                    //                         }
                    // call the recursive function to get the adapter id. It uses odm to get this, which may need to
                    // work through multiple layers-TJE
                    get_Adapter_IDs(device, diskName);
                    ret = fill_Drive_Info_Data(device);
                }
                else
                {
                    if (device->deviceVerbosity > VERBOSITY_DEFAULT)
                    {
                        printf("Warning: Parent is empty!\n");
                    }
                }
            }
            else
            {
                // print error???
                if (device->deviceVerbosity > VERBOSITY_DEFAULT)
                {
                    printf("Unable to get parent for %s\n", filename);
                    print_ODM_Error(odmerrno);
                }
            }
        }
        // done with using odm, so terminate it
        (void)odm_terminate();
        safe_free(&diskFullName);
    }
    return ret;
}

eReturnValues os_Device_Reset(M_ATTR_UNUSED tDevice* device)
{
    return NOT_SUPPORTED;
}

eReturnValues os_Bus_Reset(M_ATTR_UNUSED tDevice* device)
{
    // if unable to find another way to do this, can close and reopen with SC_FORCED_OPEN
    return NOT_SUPPORTED;
}

eReturnValues os_Controller_Reset(M_ATTR_UNUSED tDevice* device)
{
    return NOT_SUPPORTED;
}

/*
NOTES: https://www.ibm.com/docs/en/aix/7.1?topic=subsystem-scdisk-scsi-device-driver
       https://www.ibm.com/docs/en/aix/7.1?topic=subsystem-usb-mass-storage-client-device-driver

There are a lot of different SCSI device handles/types available in AIX.
Most of this code will focus on HDD, although Tape, CD, optical can all be supported.

Diagnotic mode flag (SC_DIAGNOSTIC) required for the following:
    CDIOCMD
    DKIOCMD
    DKIOLCMD - SAM disk device driver IOCTL.
    DKIOWRSE ??? writes-diagnostic use           (also CDIOWDSE)
    DKIOLWRSE - SAM disk device driver IOCTL.
    DKIOLRDSE - SAM disk device driver IOCTL.
    DKIORDSE ??? read with diagnostic sense data (also CDIORDSE)
Diagnostic mode flag/commands are sent in more or less exclusive access mode.
If open in diagnostic mode, only ioctl or close system calls can be used.

scdisk has IOCTLs without "L", scsidisk SAM driver has "L" DKIO... vs DKIOL...

SC_SINGLE = exclusive access mode flag

Devices open and close with openx/closex

IOCINFO provides lots of information about the device, including max transfer size, etc.


Other ioctls for specific commands:
    DKLOGSENSE
    DKLOGSELECT
    DKPMR - prevent media removal
    DKAMR - allow media removal
    DKEJECT - eject
    DKFORMAT - format unit (not for HDDs) - exclusive access required
    DKAUDIO - play audio
    DK_CD_MODE - CD/DVD data mode, block size, special file types, etc
    DKPRES_READKEYS - persistent reservation, read keys
    DKPRES_READRES - persistent reserve read reservations
    DKPRES_CLEAR - clear all persistent reservation keys on the device
    DKPRES_PREEMPT - persistent reservation preempt
    DKPRES_PREEMPT_ABORT - persistent reservation preempt-abort
    DKPRES_REGISTER - persistent reserve register
    DK_RWBUFFER - issue one more more write buffer commands in single IOCTL. All outstanding IO is quiesced before this
is issued

Multi-path (https://www.ibm.com/docs/en/aix/7.1?topic=management-multiple-path-io):
    DKPATHIOCMD - for multi-path capable devices. Works like DKIOCMD except that the input path is used instead of
normal path selection DKPATHIOCMD - force specific path for all subsequent IO DKPATHRWBUFFER - same as DKRWBUFFER except
using input path instead DKPATHPASSTHRU - same as DK_PASSTHRU, except input path is used DKPCMPASSTHRU - PCM specific
structure passed to PCM (Path control module) directly


DK_PASSTHRU - The DK_PASSTHRU operation differs from the DKIOCMD operation in that it does not require an openx command
with the ext argument of SC_DIAGNOSTIC. Because of this, a DK_PASSTHRU operation can be issued to devices that are in
use by other operations.
            - SC_MIX_IO requests that write data to devices are prohibited and will fail.
            - SC_QUIESCE_IO, all other I/O requests will be quiesced before the DK_PASSTHRU request is issued to the
device (no zero timeout allowed)
            - If an SC_QUIESCE_IO request has a nonzero timeout value that is too large for the device, the DK_PASSTHRU
request will be failed with a return code of -1, the errno global variable will be set to EINVAL, the einval_arg field
will be set to a value of SC_PASSTHRU_INV_TO (defined in the /usr/include/sys/scsi.h file), and the timeout_value will
be set to the largest allowed value.
            - The version field of the sc_passthru structure can be set to the value of SC_VERSION_2, and the user can
provide the following fields: variable_cdb_ptr is a pointer to a buffer that contains the Variable SCSI cdb.
                variable_cdb_length determines the length of the cdb variable to which the variable_cdb_ptr field
points.
            - The devinfo structure defines the maximum transfer size for the command

IDEPASSTHRU - only mentioned on the USB mass storage driver page for ATAPI devices -
https://www.ibm.com/docs/en/aix/7.1?topic=subsystem-usb-mass-storage-client-device-driver

Handles:
The special files that are used by the scdisk device driver include the following (listed by type of device):

Hard disk devices:
/dev/rhdisk0, /dev/rhdisk1,..., /dev/rhdiskn	Provides an interface to allow SCSI device drivers character access (raw
I/O access and control functions) to SCSI hard disks. /dev/hdisk0, /dev/hdisk1,..., /dev/hdiskn	Provides an interface to
allow SCSI device drivers block I/O access to SCSI hard disks.

CD-ROM devices:
/dev/rcd0, /dev/rcd1,..., /dev/rcdn	Provides an interface to allow SCSI device drivers character access (raw I/O access
and control functions) to SCSI CD-ROM disks. /dev/cd0, /dev/cd1,..., /dev/cdn	Provides an interface to allow SCSI
device drivers block I/O access to SCSI CD-ROM disks.

Read/write optical devices:
/dev/romd0, /dev/romd1,..., /dev/romdn	Provides an interface to allow SCSI device drivers character access (raw I/O
access and control functions) to SCSI read/write optical devices. /dev/omd0, /dev/omd1,..., /dev/omdn	Provides an
interface to allow SCSI device drivers block I/O access to SCSI read/write optical devices.

Tape devices:
/dev/rmt255, /dev/rmt255.1, /dev/rmt255.2, ..., /dev/rmt255.7	Provide an interface to allow SCSI device drivers to
access SCSI tape drives.

SCSI Adapters:
/dev/scsi0, /dev/scsi1, ... /dev/scsin	Provides an interface for all SCSI device drivers to access SCSI devices or
adapters. /dev/vscsi0, /dev/vscsi1,..., /dev/vscsin	Provide an interface to allow SCSI-2 Fast/Wide Adapter/A and SCSI-2
Differential Fast/Wide Adapter/A device drivers to access SCSI devices or adapters.
    - seems to be SCSI adapters and use SCIO<> type IOCTLs. Not sure if this is needed for what we are after. This has a
reset option though which we may want. -TJE
    - SCIORESET can do bus or LUN reset?

NVMe controller:
The /dev/nvmen special file provides interfaces to the NVMe controller device driver.

NVMe disk/namespace:
The /dev/hdiskn special file provides interfaces to the NVMe storage device driver. - flags field will set DF_SSD and
DF_NVME. DF_4B_ALINGED  specifies all host buffer addresses must be 4 byte aligned


open/openx flags:
https://www.ibm.com/docs/en/aix/7.1?topic=o-open-openat-openx-openxat-open64-open64at-open64x-open64xat-creat-creat64-subroutine


*/

static void print_Passthrough_Bus_And_Adapter_Status(uchar status_validity,
                                                     uchar scsi_bus_status,
                                                     uchar adap_status_type,
                                                     uchar adapter_status)
{
    switch (status_validity)
    {
    case 0: // no bus or adapter status
        break;
    case 1: // scsi_bus_status is valid
        printf("AIX SCSI Bus Status:\n");
        switch (scsi_bus_status & SCSI_STATUS_MASK)
        {
        case SC_GOOD_STATUS:
            printf("\tGood\n");
            break;
        case SC_CHECK_CONDITION:
            printf("\tCheck Condition\n");
            break;
        case SC_BUSY_STATUS:
            printf("\tBusy\n");
            break;
        case SC_INTMD_GOOD:
            printf("\tIntermediate Good\n");
            break;
        case SC_RESERVATION_CONFLICT:
            printf("\tReservation Conflict\n");
            break;
        case SC_COMMAND_TERMINATED:
            printf("\tCommand Terminated\n");
            break;
        case SC_QUEUE_FULL:
            printf("\tQueue Full\n");
            break;
        case SC_ACA_ACTIVE:
            printf("\tACA Active\n");
            break;
        case SC_TASK_ABORTED:
            printf("\tTask Aborted\n");
            break;
        default:
            printf("\tUnknown scsi bus status: %u\n", scsi_bus_status & SCSI_STATUS_MASK);
            break;
        }
        break;
    case 2: // adap_status_type is valid
        printf("AIX SCSI Adapter Status:\n");
        switch (adap_status_type)
        {
        case SC_ADAP_SC_ERR: // parallel SCSI adapter
            printf("\tParallel SCSI Adapter Status:\n");
            // general_card_status
            switch (adapter_status)
            {
            case SC_HOST_IO_BUS_ERR:
                printf("\t\tHost I/O Bus Error\n");
                break;
            case SC_SCSI_BUS_FAULT:
                printf("\t\tSCSI Bus Failure\n");
                break;
            case SC_CMD_TIMEOUT:
                printf("\t\tCommand Timeout\n");
                break;
            case SC_NO_DEVICE_RESPONSE:
                printf("\t\tNo Device Response\n");
                break;
            case SC_ERROR_NO_RETRY:
                printf("\t\tError Occurred - do not retry\n");
                break;
            case SC_ERROR_DELAY_LOG:
                printf("\t\tError Occurred - Only log if max retries exceeded\n");
                break;
            case SC_ADAPTER_HDW_FAILURE:
                printf("\t\tAdapter Hardware Failure\n");
                break;
            case SC_ADAPTER_SFW_FAILURE:
                printf("\t\tAdapter Microcode Failure\n");
                break;
            case SC_FUSE_OR_TERMINAL_PWR:
                printf("\t\tAdapter blown fuse or bad termination\n");
                break;
            case SC_SCSI_BUS_RESET:
                printf("\t\tAdapter detected external bus reset\n");
                break;
            }
            break;
        case SC_ADAP_SAM_ERR: // SAM-3 adapter
            printf("\tSAM-3 Adapter Status:\n");
            // adapter_status
            switch (adapter_status)
            {
            // scsi_buf.h defines these for scsi_buf
            // https://www.ibm.com/docs/en/aix/7.2?topic=structure-fields-in-scsi-buf
            case SCSI_HOST_IO_BUS_ERR:
                printf("\t\tSCSI Host I/O Bus Error\n");
                break;
            case SCSI_TRANSPORT_FAULT:
                printf("\t\tSCSI Transport Fault\n");
                break;
            case SCSI_CMD_TIMEOUT:
                printf("\t\tSCSI Command Timeout\n");
                break;
            case SCSI_NO_DEVICE_RESPONSE:
                printf("\t\tSCSI No Device Response\n");
                break;
            case SCSI_ADAPTER_HDW_FAILURE:
                printf("\t\tSCSI Adapter Hardware Failure\n");
                break;
            case SCSI_ADAPTER_SFW_FAILURE:
                printf("\t\tSCSI Adapter Microcode Failure\n");
                break;
            case SCSI_FUSE_OR_TERMINAL_PWR:
                printf("\t\tSCSI Fuse Blown or Bad Termination\n");
                break;
            case SCSI_TRANSPORT_RESET:
                printf("\t\tSCSI Transport Layer Was Reset\n");
                break;
            case SCSI_WW_NAME_CHANGE:
                printf("\t\tSCSI World Wide Name Has Changed\n");
                break;
            case SCSI_TRANSPORT_BUSY:
                printf("\t\tSCSI Transport Busy\n");
                break;
            case SCSI_TRANSPORT_DEAD:
                printf("\t\tSCSI Transport Dead\n");
                break;
            }
            break;
        default:
            printf("\tUnknown Adapter Status: %u\n", adap_status_type);
            break;
        }
        break;
    default:
        printf("Unknown value for status_validity: %u", status_validity);
        break;
    }
}

static void print_Adapter_Queue_Status(uchar adap_q_status)
{
    if (adap_q_status == 0)
    {
        printf("Adapter Queue Status: Cleared\n");
    }
    else if (adap_q_status == SC_DID_NOT_CLEAR_Q)
    {
        printf("Adapter Queue Status: Queue at the Adapter was not cleared\n");
    }
    else
    {
        printf("Adapter Queue Status: Unknown: %u\n", adap_q_status);
    }
}

// This function is not currently in use, but will work with up to 16B CDBs.
// up to 12B for older devices.
// Trying to use the big passthrough as it allows much larger CDBs and even variable length CDBs instead.
// Enable using this is we ever need it for compatibility or it does something different than normal passthrough that we
// need-TJE
//  static eReturnValues send_AIX_SCSI_Diag_IO(ScsiIoCtx *scsiIoCtx)
//  {
//      //uses the DKIOCMD when opened with the diagnostic mode flag so that this command is issued with nothing else in
//      queue
//      //and more or less exclusive access to the device.
//      //NOTE: This does not issue request sense upon an error! This will need to be done manually!
//      int         ret          = SUCCESS;
//      bool issueRequestSense = false;
//      if (scsiIoCtx->cdbLength <= 12)
//      {
//          int ioctlCode = DKIOCMD;
//          DECLARE_SEATIMER(commandTimer);
//          struct sc_iocmd aixIoCmd;
//          safe_memset(&aixIoCmd, sizeof(struct sc_iocmd), 0, sizeof(struct sc_iocmd));
//          if (scsiIoCtx->device->os_info.adapterType != AIX_ADAPTER_SCSI)
//          {
//              ioctlCode = DKIOLCMD;
//          }

//         aixIoCmd.q_tag_msg = 0;//SC_NO_Q, SC_SIMPLE_Q, SC_HEAD_OF_Q, SC_ORDERED_Q, SC_ACA_Q
//         aixIoCmd.flags = SC_QUIESCE_IO;//or SC_MIX_IO? Leaving as quiesce for now -TJE
//         aixIoCmd.q_flags = 0;//SC_Q_CLR, SC_Q_RESUME, SC_CLEAR_ACA
//         //setup flags
//         //These two are available, but not currently used
//         //#define SC_NODISC   0x80        /* don't allow disconnections */
//         //#define SC_ASYNC    0x08        /* asynchronous data xfer */
//         switch(scsiIoCtx->direction)
//         {
//         case XFER_DATA_IN:
//             aixIoCmd.flags = B_READ;
//             break;
//         case XFER_DATA_OUT:
//             aixIoCmd.flags = B_WRITE;
//             break;
//         case XFER_NO_DATA:
//             aixIoCmd.flags = B_READ;
//             break;
//         case XFER_DATA_IN_OUT:
//         case XFER_DATA_OUT_IN:
//             aixIoCmd.flags = B_READ | B_WRITE;
//             break;
//         }
//         aixIoCmd.data_length = scsiIoCtx->dataLength;
//         aixIoCmd.buffer = C_CAST(char *, scsiIoCtx->pdata);

//         if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds > 0 &&
//         scsiIoCtx->device->drive_info.defaultTimeoutSeconds > scsiIoCtx->timeout)
//         {
//             aixIoCmd.timeout_value = scsiIoCtx->device->drive_info.defaultTimeoutSeconds;
//             if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds >= AIX_MAX_CMD_TIMEOUT_SECONDS)
//             {
//                 aixIoCmd.timeout_value = UINT32_MAX;//no timeout or maximum timeout
//             }
//         }
//         else
//         {
//             if (scsiIoCtx->timeout != 0)
//             {
//                 aixIoCmd.timeout_value = scsiIoCtx->timeout;
//                 if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds >= AIX_MAX_CMD_TIMEOUT_SECONDS)
//                 {
//                     aixIoCmd.timeout_value = UINT32_MAX;//no timeout or maximum timeout
//                 }
//             }
//             else
//             {
//                 aixIoCmd.timeout_value = 15;//default to 15 second timeout
//             }
//         }
//         aixIoCmd.command_length = scsiIoCtx->cdbLength;
//         safe_memcpy(&aixIoCmd.scsi_cdb[0], 12, scsiIoCtx->cdb, scsiIoCtx->cdbLength);

//         aixIoCmd.lun = 0;//if greater than 7, must be used to ignore LUN bits in SCSI 1 commands

//         start_Timer(&commandTimer);
//         ret = ioctl(scsiIoCtx->device->os_info.fd, ioctlCode, &aixIoCmd);
//         stop_Timer(&commandTimer);
//         scsiIoCtx->device->os_info.last_error = errno;
//         if (ret < 0)
//         {
//             ret = OS_PASSTHROUGH_FAILURE;
//             if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
//             {
//                 if (scsiIoCtx->device->os_info.last_error != 0)
//                 {
//                     printf("Error: ");
//                     print_Errno_To_Screen(scsiIoCtx->device->os_info.last_error);
//                 }
//                 print_Passthrough_Bus_And_Adapter_Status(aixIoCmd.status_validity, aixIoCmd.scsi_bus_status,
//                 ioctlCode == DKIOCMD ? SC_ADAP_SC_ERR : SC_ADAP_SAM_ERR, aixIoCmd.adapter_status);
//                 print_Adapter_Queue_Status(aixIoCmd.adap_q_status);
//             }
//         }
//         if (aixIoCmd.status_validity == 0)
//         {
//             ret = SUCCESS;
//         }
//         else if (aixIoCmd.status_validity == 1)
//         {
//             switch(aixIoCmd.scsi_bus_status & SCSI_STATUS_MASK)
//             {
//             case SC_GOOD_STATUS:
//                 ret = SUCCESS;
//                 break;
//             case SC_CHECK_CONDITION:
//                 ret = SUCCESS;//succesfully issued the IO, so pass sense data back up the stack
//                 issueRequestSense = true;
//                 break;
//             case SC_BUSY_STATUS:
//             case SC_INTMD_GOOD:
//             case SC_RESERVATION_CONFLICT:
//             case SC_COMMAND_TERMINATED:
//             case SC_QUEUE_FULL:
//             case SC_ACA_ACTIVE:
//             case SC_TASK_ABORTED:
//             default:
//                 ret = OS_PASSTHROUGH_FAILURE;
//                 break;
//             }
//         }
//         else
//         {
//             ret = OS_PASSTHROUGH_FAILURE;
//         }
//         scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
//     }
//     else if (scsiIoCtx->cdbLength <= 16)
//     {
//         DECLARE_SEATIMER(commandTimer);
//         struct sc_iocmd16cdb aixIoCmd;
//         safe_memset(&aixIoCmd, sizeof(struct sc_iocmd16cdb), 0, sizeof(struct sc_iocmd16cdb));
//         aixIoCmd.q_tag_msg = 0;//SC_NO_Q, SC_SIMPLE_Q, SC_HEAD_OF_Q, SC_ORDERED_Q, SC_ACA_Q
//         aixIoCmd.flags = SC_QUIESCE_IO;//or SC_MIX_IO? Leaving as quiesce for now -TJE
//         aixIoCmd.q_flags = 0;//SC_Q_CLR, SC_Q_RESUME, SC_CLEAR_ACA
//         //setup flags
//         //These two are available, but not currently used
//         //#define SC_NODISC   0x80        /* don't allow disconnections */
//         //#define SC_ASYNC    0x08        /* asynchronous data xfer */
//         switch(scsiIoCtx->direction)
//         {
//         case XFER_DATA_IN:
//             aixIoCmd.flags = B_READ;
//             break;
//         case XFER_DATA_OUT:
//             aixIoCmd.flags = B_WRITE;
//             break;
//         case XFER_NO_DATA:
//             aixIoCmd.flags = B_READ;
//             break;
//         case XFER_DATA_IN_OUT:
//         case XFER_DATA_OUT_IN:
//             aixIoCmd.flags = B_READ | B_WRITE;
//             break;
//         }

//         aixIoCmd.data_length = scsiIoCtx->dataLength;
//         aixIoCmd.buffer = C_CAST(char *, scsiIoCtx->pdata);

//         if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds > 0 &&
//         scsiIoCtx->device->drive_info.defaultTimeoutSeconds > scsiIoCtx->timeout)
//         {
//             aixIoCmd.timeout_value = scsiIoCtx->device->drive_info.defaultTimeoutSeconds;
//             if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds >= AIX_MAX_CMD_TIMEOUT_SECONDS)
//             {
//                 aixIoCmd.timeout_value = UINT32_MAX;//no timeout or maximum timeout
//             }
//         }
//         else
//         {
//             if (scsiIoCtx->timeout != 0)
//             {
//                 aixIoCmd.timeout_value = scsiIoCtx->timeout;
//                 if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds >= AIX_MAX_CMD_TIMEOUT_SECONDS)
//                 {
//                     aixIoCmd.timeout_value = UINT32_MAX;//no timeout or maximum timeout
//                 }
//             }
//             else
//             {
//                 aixIoCmd.timeout_value = 15;//default to 15 second timeout
//             }
//         }
//         aixIoCmd.command_length = scsiIoCtx->cdbLength;
//         safe_memcpy(&aixIoCmd.scsi_cdb[0], 16, scsiIoCtx->cdb, scsiIoCtx->cdbLength);

//         aixIoCmd.lun = 0;//if greater than 7, must be used to ignore LUN bits in SCSI 1 commands

//         start_Timer(&commandTimer);
//         ret = ioctl(scsiIoCtx->device->os_info.fd, DKIOCMD16, &aixIoCmd);
//         stop_Timer(&commandTimer);
//         scsiIoCtx->device->os_info.last_error = errno;
//         if (ret < 0)
//         {
//             ret = OS_PASSTHROUGH_FAILURE;
//             if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
//             {
//                 if (scsiIoCtx->device->os_info.last_error != 0)
//                 {
//                     printf("Error: ");
//                     print_Errno_To_Screen(scsiIoCtx->device->os_info.last_error);
//                 }
//                 print_Passthrough_Bus_And_Adapter_Status(aixIoCmd.status_validity, aixIoCmd.scsi_bus_status,
//                 SC_ADAP_SAM_ERR, aixIoCmd.adapter_status); print_Adapter_Queue_Status(aixIoCmd.adap_q_status);
//             }
//         }
//         if (aixIoCmd.status_validity == 0)
//         {
//             ret = SUCCESS;
//         }
//         else if (aixIoCmd.status_validity == 1)
//         {
//             switch(aixIoCmd.scsi_bus_status & SCSI_STATUS_MASK)
//             {
//             case SC_GOOD_STATUS:
//                 ret = SUCCESS;
//                 break;
//             case SC_CHECK_CONDITION:
//                 ret = SUCCESS;//succesfully issued the IO, so pass sense data back up the stack
//                 issueRequestSense = true;
//                 break;
//             case SC_BUSY_STATUS:
//             case SC_INTMD_GOOD:
//             case SC_RESERVATION_CONFLICT:
//             case SC_COMMAND_TERMINATED:
//             case SC_QUEUE_FULL:
//             case SC_ACA_ACTIVE:
//             case SC_TASK_ABORTED:
//             default:
//                 ret = OS_PASSTHROUGH_FAILURE;
//                 break;
//             }
//         }
//         else
//         {
//             ret = OS_PASSTHROUGH_FAILURE;
//         }
//         scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
//     }
//     else
//     {
//         //TODO: Send this with the passthrough command instead???
//         //      There are not a lot of commands larger than 16B being sent today -TJE
//         ret = OS_COMMAND_NOT_AVAILABLE;
//     }
//     if (issueRequestSense && scsiIoCtx->psense)
//     {
//         int ioctlCode = DKIOCMD;
//         DECLARE_SEATIMER(commandTimer);
//         struct sc_iocmd aixIoCmd;
//         safe_memset(&aixIoCmd, sizeof(struct sc_iocmd), 0, sizeof(struct sc_iocmd));
//         if (scsiIoCtx->device->os_info.adapterType != AIX_ADAPTER_SCSI)
//         {
//             ioctlCode = DKIOLCMD;
//         }

//         aixIoCmd.q_tag_msg = 0;//SC_NO_Q, SC_SIMPLE_Q, SC_HEAD_OF_Q, SC_ORDERED_Q, SC_ACA_Q
//         aixIoCmd.flags = SC_QUIESCE_IO;//or SC_MIX_IO? Leaving as quiesce for now -TJE
//         aixIoCmd.q_flags = 0;//SC_Q_CLR, SC_Q_RESUME, SC_CLEAR_ACA
//         aixIoCmd.flags = B_READ;

//         aixIoCmd.data_length = scsiIoCtx->senseDataSize;
//         aixIoCmd.buffer = C_CAST(char *, scsiIoCtx->psense);
//         aixIoCmd.timeout_value = 15;//default to 15 second timeout
//         //setup the CDB
//         aixIoCmd.scsi_cdb[0] = REQUEST_SENSE_CMD;
//         aixIoCmd.scsi_cdb[1] = 0;//TODO: Descriptor bit? Can either track support early on in discovery, or infer
//         from the command that was sent what to do-TJE aixIoCmd.scsi_cdb[2] = RESERVED; aixIoCmd.scsi_cdb[3] =
//         RESERVED; aixIoCmd.scsi_cdb[4] = M_Min(252, scsiIoCtx->senseDataSize); aixIoCmd.scsi_cdb[5] = 0;//control
//         byte

//         aixIoCmd.lun = 0;//if greater than 7, must be used to ignore LUN bits in SCSI 1 commands

//         start_Timer(&commandTimer);
//         ret = ioctl(scsiIoCtx->device->os_info.fd, ioctlCode, &aixIoCmd);
//         stop_Timer(&commandTimer);
//         scsiIoCtx->device->os_info.last_error = errno;
//         if (ret < 0)
//         {
//             ret = OS_PASSTHROUGH_FAILURE;
//             if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
//             {
//                 if (scsiIoCtx->device->os_info.last_error != 0)
//                 {
//                     printf("Error: ");
//                     print_Errno_To_Screen(scsiIoCtx->device->os_info.last_error);
//                 }
//                 print_Passthrough_Bus_And_Adapter_Status(aixIoCmd.status_validity, aixIoCmd.scsi_bus_status,
//                 ioctlCode == DKIOCMD ? SC_ADAP_SC_ERR : SC_ADAP_SAM_ERR, aixIoCmd.adapter_status);
//                 print_Adapter_Queue_Status(aixIoCmd.adap_q_status);
//             }
//         }
//         if (aixIoCmd.status_validity == 0)
//         {
//             ret = SUCCESS;
//         }
//         else if (aixIoCmd.status_validity == 1)
//         {
//             switch(aixIoCmd.scsi_bus_status & SCSI_STATUS_MASK)
//             {
//             case SC_GOOD_STATUS:
//                 ret = SUCCESS;
//                 break;
//             case SC_CHECK_CONDITION:
//                 ret = OS_PASSTHROUGH_FAILURE;//this means that something bad happened during request sense, so
//                 consider this a bigger failure-TJE break;
//             case SC_BUSY_STATUS:
//             case SC_INTMD_GOOD:
//             case SC_RESERVATION_CONFLICT:
//             case SC_COMMAND_TERMINATED:
//             case SC_QUEUE_FULL:
//             case SC_ACA_ACTIVE:
//             case SC_TASK_ABORTED:
//             default:
//                 ret = OS_PASSTHROUGH_FAILURE;
//                 break;
//             }
//         }
//         else
//         {
//             ret = OS_PASSTHROUGH_FAILURE;
//         }
//         //add the extra time for the request sense
//         scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds += get_Nano_Seconds(commandTimer);
//     }
//     else if (scsiIoCtx->psense)
//     {
//         safe_memset(scsiIoCtx->psense, scsiIoCtx->senseDataSize, 0, scsiIoCtx->senseDataSize);
//     }
//     return ret;
// }

static eReturnValues send_AIX_SCSI_Passthrough(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    // uses passthrough structure
    DECLARE_SEATIMER(commandTimer);
    struct sc_passthru aixPassthrough;
    safe_memset(&aixPassthrough, sizeof(struct sc_passthru), 0, sizeof(struct sc_passthru));

    aixPassthrough.version =
        SCSI_VERSION_2;           // TODO: version 1 vs version 2? Probably only helpful on old AIX installations
    aixPassthrough.q_tag_msg = 0; // SC_NO_Q, SC_SIMPLE_Q, SC_HEAD_OF_Q, SC_ORDERED_Q, SC_ACA_Q
    aixPassthrough.devflags  = SC_QUIESCE_IO; // or SC_MIX_IO? Leaving as quiesce for now -TJE
    aixPassthrough.q_flags   = 0;             // SC_Q_CLR, SC_Q_RESUME, SC_CLEAR_ACA
    // setup flags
    // These two are available, but not currently used
    // #define SC_NODISC   0x80        /* don't allow disconnections */
    // #define SC_ASYNC    0x08        /* asynchronous data xfer */
    switch (scsiIoCtx->direction)
    {
        // NOLINTBEGIN(bugprone-branch-clone)
    case XFER_DATA_IN:
    case XFER_NO_DATA:
        aixPassthrough.flags = B_READ;
        break;
    case XFER_DATA_OUT:
        aixPassthrough.flags = B_WRITE;
        break;
    case XFER_DATA_IN_OUT:
    case XFER_DATA_OUT_IN:
        aixPassthrough.flags = B_READ | B_WRITE;
        break;
        // NOLINTEND(bugprone-branch-clone)
    }

    aixPassthrough.command_length = scsiIoCtx->cdbLength;
    if (scsiIoCtx->cdbLength > SC_PASSTHRU_CDB_LEN)
    {
        // TODO: This allows for variable length CDBs with the following fields:
        // variable_cdb_length
        // variable_cdb_ptr
        //       This may be useful to use in the future. -TJE
        return OS_COMMAND_NOT_AVAILABLE;
    }
    safe_memcpy(&aixPassthrough.scsi_cdb[0], SC_PASSTHRU_CDB_LEN, scsiIoCtx->cdb, scsiIoCtx->cdbLength);
    aixPassthrough.autosense_length     = scsiIoCtx->senseDataSize;
    aixPassthrough.data_length          = scsiIoCtx->dataLength;
    aixPassthrough.buffer               = C_CAST(char*, scsiIoCtx->pdata);
    aixPassthrough.autosense_buffer_ptr = C_CAST(char*, scsiIoCtx->psense);

    aixPassthrough.scsi_id         = 0; // TODO: Do we need to discover this and save it?
    aixPassthrough.lun_id          = 0; // TODO: Do we need to discover this and save it?
    aixPassthrough.world_wide_name = 0; // TODO: Discover and save this to pass it here???
    aixPassthrough.node_name       = 0; // TODO: Discover and save this to pass it here???

    if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds > 0 &&
        scsiIoCtx->device->drive_info.defaultTimeoutSeconds > scsiIoCtx->timeout)
    {
        aixPassthrough.timeout_value = scsiIoCtx->device->drive_info.defaultTimeoutSeconds;
        if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds >= AIX_MAX_CMD_TIMEOUT_SECONDS)
        {
            aixPassthrough.timeout_value = UINT32_MAX; // no timeout or maximum timeout
        }
    }
    else
    {
        if (scsiIoCtx->timeout != 0)
        {
            aixPassthrough.timeout_value = scsiIoCtx->timeout;
            if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds >= AIX_MAX_CMD_TIMEOUT_SECONDS)
            {
                aixPassthrough.timeout_value = UINT32_MAX; // no timeout or maximum timeout
            }
        }
        else
        {
            aixPassthrough.timeout_value = 15; // default to 15 second timeout
        }
    }

    start_Timer(&commandTimer);
    int ioctlResult = ioctl(scsiIoCtx->device->os_info.fd, DK_PASSTHRU, &aixPassthrough);
    stop_Timer(&commandTimer);

    scsiIoCtx->device->os_info.last_error = errno;
    if (ioctlResult < 0)
    {
        ret = OS_PASSTHROUGH_FAILURE;
        if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
        {
            // https://github.com/RobinTMiller/dt/blob/master/scsilib-aix.c
            if (scsiIoCtx->device->os_info.last_error != 0)
            {
                printf("Error: ");
                print_Errno_To_Screen(scsiIoCtx->device->os_info.last_error);
            }
            print_Passthrough_Bus_And_Adapter_Status(aixPassthrough.status_validity, aixPassthrough.scsi_bus_status,
                                                     aixPassthrough.adap_status_type, aixPassthrough.adapter_status);
            if (aixPassthrough.adap_set_flags & SC_AUTOSENSE_DATA_VALID)
            {
                printf("Adapter auto-sense data is valid\n");
            }
            if (aixPassthrough.adap_set_flags & SC_RET_ID)
            {
                printf("SCSI ID is different than was provided and the adapter has updated it to: %" PRIu64 "\n",
                       aixPassthrough.scsi_id);
            }
            printf("Additional Device Status: %u\n", aixPassthrough.add_device_status);
            print_Adapter_Queue_Status(aixPassthrough.adap_q_status);

            if (scsiIoCtx->device->os_info.last_error == EINVAL)
            {
                // TODO: Some of these, upon error will return an allowed value. Look at showing these in this error
                // output.-TJE
                printf("Invalid field in sc_passthru:\n");
                switch (aixPassthrough.einval_arg)
                {
                case SC_PASSTHRU_INV_VERS:
                    printf("\tInvalid Version\n");
                    break;
                case SC_PASSTHRU_INV_Q_TAG_MSG:
                    printf("\tQ Tag field is invalid\n");
                    break;
                case SC_PASSTHRU_INV_FLAGS:
                    printf("\tInvalid flags\n");
                    break;
                case SC_PASSTHRU_INV_DEVFLAGS:
                    printf("\tInvalid device flags\n");
                    break;
                case SC_PASSTHRU_INV_Q_FLAGS:
                    printf("\tInvalid Queue flags\n");
                    break;
                case SC_PASSTHRU_INV_CDB_LEN:
                    printf("\tInvalid CDB length\n");
                    break;
                case SC_PASSTHRU_INV_AS_LEN:
                    printf("\tInvalid autosense length\n");
                    break;
                case SC_PASSTHRU_INV_CDB:
                    printf("\tInvalid CDB\n");
                    break;
                case SC_PASSTHRU_INV_TO:
                    // Supposedly this can tell what an appropriate maximum timeout is in the timeout offset when this
                    // occurs.
                    printf("\tInvalid timeout\n");
                    break;
                case SC_PASSTHRU_INV_D_LEN:
                    printf("\tInvalid data length\n");
                    break;
                case SC_PASSTHRU_INV_SID:
                    printf("\tInvalid SCSI ID\n");
                    break;
                case SC_PASSTHRU_INV_LUN:
                    printf("\tInvalid LUN ID\n");
                    break;
                case SC_PASSTHRU_INV_BUFF:
                    printf("\tInvalid data buffer pointer\n");
                    break;
                case SC_PASSTHRU_INV_AS_BUFF:
                    printf("\tInvalid autosense buffer pointer\n");
                    break;
                case SC_PASSTHRU_INV_VAR_CDB_LEN:
                    printf("\tInvalid variable CDB length\n");
                    break;
                case SC_PASSTHRU_INV_VAR_CDB:
                    printf("\tInvalid variable length CDB pointer\n");
                    break;
                default:
                    printf("\tUnknown invalid field: %u\n", aixPassthrough.einval_arg);
                    break;
                }
            }
        }
    }
    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    return ret;
}

// IOCTL IDEIOIDENT with struct ide_identify may be helpful to identify when IDE or SATA passthrough are available-TJE

// NOTE: This issues the IDE_ATA passthrough. There is a separate ATAPI passthrough if we need to handle those
//       using that IOCTL instead of the SCSI passthrough IOCTLs. Can be done later as we currently do not handle
//       CD/DVDs, etc -TJE
static eReturnValues send_AIX_IDE_ATA_Passthrough(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    // sends the IDE passthrough IOCTL
    DECLARE_SEATIMER(commandTimer);
    struct ide_ata_passthru idePassthrough; // 28bit commands only
    if (!scsiIoCtx->pAtaCmdOpts)
    {
        return BAD_PARAMETER;
    }

    if (scsiIoCtx->pAtaCmdOpts->commandType != ATA_CMD_TYPE_TASKFILE)
    {
        // only possible to issue 28bit commands in ide passthrough. SATA passthrough allows more though-TJE
        return OS_COMMAND_NOT_AVAILABLE;
    }

    safe_memset(&idePassthrough, sizeof(struct ide_ata_passthru), 0, sizeof(struct ide_ata_passthru));

    idePassthrough.version = IDE_ATA_PASSTHRU_VERSION_1;

    idePassthrough.flags = ATA_CHS_MODE; // start by assuming CHS until we find the LBA mode bit
    if (scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead & LBA_MODE_BIT)
    {
        idePassthrough.flags = ATA_LBA_MODE;
    }
    // NOTE: There is a flag for ATA_BUS_RESET that may be useful for implementing a reset -TJE

    switch (scsiIoCtx->pAtaCmdOpts->commandDirection)
    {
    case XFER_DATA_IN:
        idePassthrough.flags |= IDE_PASSTHRU_READ;
        break;
    case XFER_DATA_OUT:
        // no flags for write that I can find in ide.h
        // if needed we can use B_WRITE from the scsi flags, but skipping for now - TJE
        // idePassthrough.flags = B_WRITE;
        break;
    case XFER_NO_DATA:
        idePassthrough.flags |= IDE_PASSTHRU_READ;
        break;
    case XFER_DATA_IN_OUT:
    case XFER_DATA_OUT_IN:
        return BAD_PARAMETER;
        break;
    }
    idePassthrough.buffsize = scsiIoCtx->pAtaCmdOpts->dataSize;
    idePassthrough.data_ptr = scsiIoCtx->pAtaCmdOpts->ptrData;

    if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds > 0 &&
        scsiIoCtx->device->drive_info.defaultTimeoutSeconds > scsiIoCtx->pAtaCmdOpts->timeout)
    {
        idePassthrough.timeout_value = scsiIoCtx->device->drive_info.defaultTimeoutSeconds;
        if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds >= AIX_MAX_CMD_TIMEOUT_SECONDS)
        {
            idePassthrough.timeout_value = UINT32_MAX; // no timeout or maximum timeout
        }
    }
    else
    {
        if (scsiIoCtx->pAtaCmdOpts->timeout != 0)
        {
            idePassthrough.timeout_value = scsiIoCtx->pAtaCmdOpts->timeout;
            if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds >= AIX_MAX_CMD_TIMEOUT_SECONDS)
            {
                idePassthrough.timeout_value = UINT32_MAX; // no timeout or maximum timeout
            }
        }
        else
        {
            idePassthrough.timeout_value = 15; // default to 15 second timeout
        }
    }

    // now set the command registers
    idePassthrough.ata_cmd.features   = scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature;
    idePassthrough.ata_cmd.sector_cnt = scsiIoCtx->pAtaCmdOpts->tfr.SectorCount;
    idePassthrough.ata_cmd.lba_low    = scsiIoCtx->pAtaCmdOpts->tfr.LbaLow;
    idePassthrough.ata_cmd.lba_mid    = scsiIoCtx->pAtaCmdOpts->tfr.LbaMid;
    idePassthrough.ata_cmd.lba_high   = scsiIoCtx->pAtaCmdOpts->tfr.LbaHi;
    idePassthrough.ata_cmd.device     = scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead;
    idePassthrough.ata_cmd.command    = scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus;

    start_Timer(&commandTimer);
    int ioctlResult = ioctl(scsiIoCtx->device->os_info.fd, IDEPASSTHRU, &idePassthrough);
    stop_Timer(&commandTimer);

    scsiIoCtx->device->os_info.last_error = errno;
    if (ioctlResult < 0)
    {
        ret = OS_PASSTHROUGH_FAILURE;
        if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
        {
            if (scsiIoCtx->device->os_info.last_error != 0)
            {
                printf("Error: ");
                print_Errno_To_Screen(scsiIoCtx->device->os_info.last_error);
            }
        }
    }

    // set returned status and error registers. It looks like that is all we will get -TJE
    scsiIoCtx->pAtaCmdOpts->rtfr.status = idePassthrough.ata_status;
    scsiIoCtx->pAtaCmdOpts->rtfr.error  = idePassthrough.ata_error;

    // resid is another part of the structure. I'm guessing this is a count of what data was or was not transferred?
    // -TJE

    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    return ret;
}

static eReturnValues send_AIX_IDE_ATAPI_Passthrough(ScsiIoCtx* scsiIoCtx)
{
    eReturnValues ret = SUCCESS;
    // sends the IDE passthrough IOCTL
    DECLARE_SEATIMER(commandTimer);
    struct ide_atapi_passthru idePassthrough; // 12 to 16B CDBs only
    safe_memset(&idePassthrough, sizeof(struct ide_atapi_passthru), 0, sizeof(struct ide_atapi_passthru));

    idePassthrough.ide_device = 0; // TODO: fill this in with target device ID

    idePassthrough.flags = ATA_CHS_MODE;
    // NOTE: There is a flag for ATA_BUS_RESET that may be useful for implementing a reset -TJE

    switch (scsiIoCtx->direction)
    {
    case XFER_DATA_IN:
        idePassthrough.flags |= IDE_PASSTHRU_READ;
        break;
    case XFER_DATA_OUT:
        // no flags for write that I can find in ide.h
        // if needed we can use B_WRITE from the scsi flags, but skipping for now - TJE
        // idePassthrough.flags = B_WRITE;
        break;
    case XFER_NO_DATA:
        idePassthrough.flags |= IDE_PASSTHRU_READ;
        break;
    case XFER_DATA_IN_OUT:
    case XFER_DATA_OUT_IN:
        return BAD_PARAMETER;
        break;
    }
    idePassthrough.buffsize = scsiIoCtx->dataLength;
    idePassthrough.data_ptr = scsiIoCtx->pdata;

    if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds > 0 &&
        scsiIoCtx->device->drive_info.defaultTimeoutSeconds > scsiIoCtx->timeout)
    {
        idePassthrough.timeout_value = scsiIoCtx->device->drive_info.defaultTimeoutSeconds;
        if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds >= AIX_MAX_CMD_TIMEOUT_SECONDS)
        {
            idePassthrough.timeout_value = UINT32_MAX; // no timeout or maximum timeout
        }
    }
    else
    {
        if (scsiIoCtx->timeout != 0)
        {
            idePassthrough.timeout_value = scsiIoCtx->timeout;
            if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds >= AIX_MAX_CMD_TIMEOUT_SECONDS)
            {
                idePassthrough.timeout_value = UINT32_MAX; // no timeout or maximum timeout
            }
        }
        else
        {
            idePassthrough.timeout_value = 15; // default to 15 second timeout
        }
    }

#if defined IDE_PASSTHRU_VERSION_01
    idePassthrough.rsv0 = IDE_PASSTHRU_VERSION_01; // when this is set we can pass the sense data and sense length in.
                                                   // otherwise you have to request sense manually on error :/
    idePassthrough.sense_data_length = scsiIoCtx->senseDataSize;
    idePassthrough.sense_data        = scsiIoCtx->psense;
#endif

    // now set the cdb
    idePassthrough.atapi_cmd.length =
        12; // ATAPI supports up to 12 or up to 16B commands. So this is set to 12 or 16 even if the CDB is smaller.-TJE
    if (scsiIoCtx->cdbLength > 12)
    {
        idePassthrough.atapi_cmd.length = 16;
        if (scsiIoCtx->cdbLength > 16)
        {
            return BAD_PARAMETER; // this should not happen on any atapi device since the limit is fixed to 16B
        }
    }
    idePassthrough.atapi_cmd.resvd          = RESERVED;
    idePassthrough.atapi_cmd.resvd1         = RESERVED;
    idePassthrough.atapi_cmd.resvd2         = RESERVED;
    idePassthrough.atapi_cmd.packet.op_code = scsiIoCtx->cdb[OPERATION_CODE];
    safe_memcpy(&idePassthrough.atapi_cmd.packet.bytes[0], 15, &scsiIoCtx->cdb[1],
                M_Min(15, scsiIoCtx->cdbLength - 1)); // this holds remaining bytes after opcode, hence -1 from length

    start_Timer(&commandTimer);
    int ioctlResult = ioctl(scsiIoCtx->device->os_info.fd, IDEPASSTHRU, &idePassthrough);
    stop_Timer(&commandTimer);

    scsiIoCtx->device->os_info.last_error = errno;
    if (ioctlResult < 0)
    {
        ret = OS_PASSTHROUGH_FAILURE;
        if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
        {
            if (scsiIoCtx->device->os_info.last_error != 0)
            {
                printf("Error: ");
                print_Errno_To_Screen(scsiIoCtx->device->os_info.last_error);
            }
            printf("IDE ATAPI Passthru Status: %02" PRIX8 "h\n", idePassthrough.ata_status);
            printf("IDE ATAPI Passthru Error:  %02" PRIX8 "h\n", idePassthrough.ata_error);
        }
    }

#if !defined IDE_PASSTHRU_VERSION_01
    // TODO: check if the status/error indicated a problem, then issue request sense
    //       This code is likely only needed for older versions of AIX. Fill it in only when needed. Some of it has been
    //       started -TJE
    if (idePassthrough.ata_status & BIT0)
    {
        // Check condition occurred
        // setup fixed format data for now
        struct ide_atapi_passthru requestSensePT;
        DECLARE_SEATIMER(rscommandTimer);
        DECLARE_ZERO_INIT_ARRAY(uint8_t, localSenseData, SPC3_SENSE_LEN);
        uint8_t senseKey = M_Nibble1(idePassthrough.ata_error); // bits 7:4 contain the sense key
        safe_memset(scsiIoCtx->psense, scsiIoCtx->senseDataSize, 0, scsiIoCtx->senseDataSize);
        scsiIoCtx->psense[0] = 0x70; // fixed format
        scsiIoCtx->psense[2] = senseKey;
        if (idePassthrough.ata_error & BIT0)
        {
            scsiIoCtx->psense[2] |= BIT5; // illegal length indicator
        }
        if (idePassthrough.ata_error & BIT1)
        {
            scsiIoCtx->psense[2] |= BIT6; // End of media
        }

        // try a request sense and return this if everything works alright
        safe_memset(&requestSensePT, sizeof(struct ide_atapi_passthru), 0, sizeof(struct ide_atapi_passthru));
        requestSensePT.ide_device = 0; // TODO: fill this in with target device ID
        requestSensePT.flags      = ATA_CHS_MODE;
        requestSensePT.flags |= IDE_PASSTHRU_READ;

        requestSensePT.buffsize = SPC3_SENSE_LEN;
        requestSensePT.data_ptr = localSenseData;
        requestSensePT.timeout  = 15;

        requestSensePT.atapi_cmd.length = 12; // ATAPI supports up to 12 or up to 16B commands. So this is set to 12 or
                                              // 16 even if the CDB is smaller.-TJE
        requestSensePT.atapi_cmd.resvd           = RESERVED;
        requestSensePT.atapi_cmd.resvd1          = RESERVED;
        requestSensePT.atapi_cmd.resvd2          = RESERVED;
        requestSensePT.atapi_cmd.packet.op_code  = REQUEST_SENSE_CMD;
        requestSensePT.atapi_cmd.packet.bytes[0] = 0; // MMC devices (CD/DVD) will never support descriptor mode. SSC
                                                      // does allow descriptors though. TODO: handling of descriptor bit
        requestSensePT.atapi_cmd.packet.bytes[1] = RESERVED;
        requestSensePT.atapi_cmd.packet.bytes[3] = RESERVED;
        requestSensePT.atapi_cmd.packet.bytes[4] = M_Min(252, scsiIoCtx->senseDataSize);
        requestSensePT.atapi_cmd.packet.bytes[5] = 0; // control byte

        start_Timer(&commandTimer);
        ioctlResult = ioctl(scsiIoCtx->device->os_info.fd, IDEPASSTHRU, &requestSensePT);
        stop_Timer(&commandTimer);

        if (ioctlResult >= 0)
        {
            // return the requested sense data if the sense key matches
            // NOTE: Assuming fixed format
            if (senseKey == M_Nibble0(localSenseData[2])
            {
                safe_memcpy(scsiIoCtx->psense, scsiIoCtx->senseDataSize, localSenseData,
                            M_Min(scsiIoCtx->senseDataSize, SPC3_SENSE_LEN));
            }
        }
    }
#endif //! IDE_PASSTHRU_VERSION_01

    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    return ret;
}

static eReturnValues send_AIX_SATA_Passthrough(ScsiIoCtx* scsiIoCtx)
{
    // sends the SATA passthrough IOCTL
    eReturnValues ret = SUCCESS;
    DECLARE_SEATIMER(commandTimer);
    struct sata_passthru sataPassthrough;
    if (!scsiIoCtx->pAtaCmdOpts)
    {
        return BAD_PARAMETER;
    }

    if (scsiIoCtx->pAtaCmdOpts->commandType == ATA_CMD_TYPE_COMPLETE_TASKFILE)
    {
        // is is not possible to issue the commands using AUX or ICC registers using this passthrough -TJE
        return OS_COMMAND_NOT_AVAILABLE;
    }

    safe_memset(&sataPassthrough, sizeof(struct sata_passthru), 0, sizeof(struct sata_passthru));

    sataPassthrough.version =
        0; // I don't see a defined version in ide.h, so setting zero as seems to be how most of these work in AIX-TJE

    sataPassthrough.flags = ATA_CHS_MODE; // start by assuming CHS until we find the LBA mode bit
    if (scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead & LBA_MODE_BIT)
    {
        sataPassthrough.flags = ATA_LBA_MODE;
    }
    sataPassthrough.ata_cmd.flags = sataPassthrough.flags; // seems like this is duplicated, but set it up too-TJE
    // NOTE: There is a flag for ATA_BUS_RESET that may be useful for implementing a reset -TJE

    switch (scsiIoCtx->pAtaCmdOpts->commandDirection)
    {
    case XFER_DATA_IN:
        sataPassthrough.flags |= SATA_PASSTHRU_READ;
        break;
    case XFER_DATA_OUT:
        // no flags for write that I can find in ide.h
        // if needed we can use B_WRITE from the scsi flags, but skipping for now - TJE
        // sataPassthrough.flags = B_WRITE;
        break;
    case XFER_NO_DATA:
        sataPassthrough.flags |= SATA_PASSTHRU_READ;
        break;
    case XFER_DATA_IN_OUT:
    case XFER_DATA_OUT_IN:
        return BAD_PARAMETER;
        break;
    }
    sataPassthrough.buffsize = scsiIoCtx->pAtaCmdOpts->dataSize;
    sataPassthrough.data_ptr = scsiIoCtx->pAtaCmdOpts->ptrData;

    if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds > 0 &&
        scsiIoCtx->device->drive_info.defaultTimeoutSeconds > scsiIoCtx->pAtaCmdOpts->timeout)
    {
        sataPassthrough.timeout_value = scsiIoCtx->device->drive_info.defaultTimeoutSeconds;
        if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds >= AIX_MAX_CMD_TIMEOUT_SECONDS)
        {
            sataPassthrough.timeout_value = UINT32_MAX; // no timeout or maximum timeout
        }
    }
    else
    {
        if (scsiIoCtx->pAtaCmdOpts->timeout != UINT32_C(0))
        {
            sataPassthrough.timeout_value = scsiIoCtx->pAtaCmdOpts->timeout;
            if (scsiIoCtx->device->drive_info.defaultTimeoutSeconds >= AIX_MAX_CMD_TIMEOUT_SECONDS)
            {
                sataPassthrough.timeout_value = UINT32_MAX; // no timeout or maximum timeout
            }
        }
        else
        {
            sataPassthrough.timeout_value = UINT32_C(15); // default to 15 second timeout
        }
    }

    // set xfer_flag
    switch (scsiIoCtx->pAtaCmdOpts->commadProtocol)
    {
        // NOLINTBEGIN(bugprone-branch-clone)
    case ATA_PROTOCOL_PIO:
        sataPassthrough.xfer_flag |= ATA_PIO_XFER;
        break;
    case ATA_PROTOCOL_DMA:
    case ATA_PROTOCOL_DMA_FPDMA:
    case ATA_PROTOCOL_DMA_QUE:
    case ATA_PROTOCOL_UDMA:
        sataPassthrough.xfer_flag |= ATA_DMA_XFER;
        break;
    default:
        break;
        // NOLINTEND(bugprone-branch-clone)
    }

    // Setup the ext command structure.
    // NOTE: The regular structure will also be setup as it has other flags, etc that can be filled in that
    //       are not part of or duplicated in the ext command structure. It is not clear if those flags must also be
    //       set or not -TJE
    if (scsiIoCtx->pAtaCmdOpts->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
    {
        sataPassthrough.xfer_flag |= ATA_EXT_CMD;
        sataPassthrough.ata_cmd_ext.feature_ext[0]        = scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature;
        sataPassthrough.ata_cmd_ext.feature_ext[1]        = scsiIoCtx->pAtaCmdOpts->tfr.Feature48;
        sataPassthrough.ata_cmd_ext.sector_cnt_cmd_ext[0] = scsiIoCtx->pAtaCmdOpts->tfr.SectorCount;
        sataPassthrough.ata_cmd_ext.sector_cnt_cmd_ext[1] = scsiIoCtx->pAtaCmdOpts->tfr.SectorCount48;
        sataPassthrough.ata_cmd_ext.lba_high_ext[0]       = scsiIoCtx->pAtaCmdOpts->tfr.LbaHi;
        sataPassthrough.ata_cmd_ext.lba_high_ext[1]       = scsiIoCtx->pAtaCmdOpts->tfr.LbaHi48;
        sataPassthrough.ata_cmd_ext.lba_mid_ext[0]        = scsiIoCtx->pAtaCmdOpts->tfr.LbaMid;
        sataPassthrough.ata_cmd_ext.lba_mid_ext[1]        = scsiIoCtx->pAtaCmdOpts->tfr.LbaMid48;
        sataPassthrough.ata_cmd_ext.lba_low_ext[0]        = scsiIoCtx->pAtaCmdOpts->tfr.LbaLow;
        sataPassthrough.ata_cmd_ext.lba_low_ext[1]        = scsiIoCtx->pAtaCmdOpts->tfr.LbaLow48;
        sataPassthrough.ata_cmd_ext.device                = scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead;
        sataPassthrough.ata_cmd_ext.command               = scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus;
    }
    else
    {
        // now set the command registers
        sataPassthrough.ata_cmd.feature        = scsiIoCtx->pAtaCmdOpts->tfr.ErrorFeature;
        sataPassthrough.ata_cmd.sector_cnt_cmd = scsiIoCtx->pAtaCmdOpts->tfr.SectorCount;
        // note: can use CHS offsets or set the LBA as one value.
        sataPassthrough.ata_cmd.startblk.chs.sector = scsiIoCtx->pAtaCmdOpts->tfr.LbaLow;
        sataPassthrough.ata_cmd.startblk.chs.cyl_lo = scsiIoCtx->pAtaCmdOpts->tfr.LbaMid;
        sataPassthrough.ata_cmd.startblk.chs.cyl_hi = scsiIoCtx->pAtaCmdOpts->tfr.LbaHi;
        // only setting the "head" or upper-most LBA bits since the other device handles dev bit
        sataPassthrough.ata_cmd.startblk.chs.head = M_Nibble0(scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead);
        sataPassthrough.ata_cmd.device            = 0; // Dev bit set to 0
        if (scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead & DEVICE_SELECT_BIT)
        {
            sataPassthrough.ata_cmd.device = 1; // DEV bit set to 1
        }
        sataPassthrough.ata_cmd.command = scsiIoCtx->pAtaCmdOpts->tfr.CommandStatus;
    }

    sataPassthrough.sata_address = 0; // where do we get this???

    start_Timer(&commandTimer);
    int ioctlResult = ioctl(scsiIoCtx->device->os_info.fd, SATAPASSTHRU, &sataPassthrough);
    stop_Timer(&commandTimer);

    scsiIoCtx->device->os_info.last_error = errno;
    if (ioctlResult < 0)
    {
        ret = OS_PASSTHROUGH_FAILURE;
        if (VERBOSITY_COMMAND_VERBOSE <= scsiIoCtx->device->deviceVerbosity)
        {
            if (scsiIoCtx->device->os_info.last_error != 0)
            {
                printf("Error: ");
                print_Errno_To_Screen(scsiIoCtx->device->os_info.last_error);
            }
        }
        // adapter_set_flags will have some output information upon completion to detect errors-TJE
        // status_validity will be set to indicate these errors:
        // #define	ATA_IDE_STATUS		1	/* ata.status is valid */
        // #define ATA_ERROR_VALID		2	/* ata.error reflects error */
        // #define ATA_DIAGNOSTICS_ERROR	4	/* adapter diagnostics reflects error*/
        // #define ATA_SMART_VALID    ATA_DIAGNOSTICS_ERROR
        // #define ATA_CMD_TIMEOUT		0x08	/* adapter timeout of command	*/
        // #define ATA_NO_DEVICE_RESPONSE	0x10	/* device continually busy	*/
        // #define ATA_IDE_BUS_RESET	0x20	/* adapter reset the bus	*/
        // #define ATA_IDE_DMA_ERROR	0x40	/* DMA error occurred   	*/
        // #define ATA_IDE_DMA_NORES	0x80	/* DMA Resource error occured   */
    }

    // NOLINTBEGIN(bugprone-branch-clone)
    if (scsiIoCtx->pAtaCmdOpts->commandType == ATA_CMD_TYPE_EXTENDED_TASKFILE)
    {
        scsiIoCtx->pAtaCmdOpts->rtfr.status    = sataPassthrough.ata_cmd_ext.status;
        scsiIoCtx->pAtaCmdOpts->rtfr.error     = sataPassthrough.ata_cmd_ext.errval;
        scsiIoCtx->pAtaCmdOpts->rtfr.secCnt    = sataPassthrough.ata_cmd_ext.sector_cnt_ret_ext[0];
        scsiIoCtx->pAtaCmdOpts->rtfr.secCntExt = sataPassthrough.ata_cmd_ext.sector_cnt_ret_ext[1];
        scsiIoCtx->pAtaCmdOpts->rtfr.lbaLow    = sataPassthrough.ata_cmd_ext.endblk_ext[0];
        scsiIoCtx->pAtaCmdOpts->rtfr.lbaMid    = sataPassthrough.ata_cmd_ext.endblk_ext[1];
        scsiIoCtx->pAtaCmdOpts->rtfr.lbaHi     = sataPassthrough.ata_cmd_ext.endblk_ext[2];
        scsiIoCtx->pAtaCmdOpts->rtfr.lbaLowExt = sataPassthrough.ata_cmd_ext.endblk_ext[3];
        scsiIoCtx->pAtaCmdOpts->rtfr.lbaMidExt = sataPassthrough.ata_cmd_ext.endblk_ext[4];
        scsiIoCtx->pAtaCmdOpts->rtfr.lbaHiExt  = sataPassthrough.ata_cmd_ext.endblk_ext[5];
    }
    else
    {
        scsiIoCtx->pAtaCmdOpts->rtfr.status = sataPassthrough.ata_cmd.status;
        scsiIoCtx->pAtaCmdOpts->rtfr.error  = sataPassthrough.ata_cmd.errval;
        scsiIoCtx->pAtaCmdOpts->rtfr.secCnt = sataPassthrough.ata_cmd.sector_cnt_ret;
        scsiIoCtx->pAtaCmdOpts->rtfr.lbaLow = sataPassthrough.ata_cmd.endblk.chs.sector;
        scsiIoCtx->pAtaCmdOpts->rtfr.lbaMid = sataPassthrough.ata_cmd.endblk.chs.cyl_lo;
        scsiIoCtx->pAtaCmdOpts->rtfr.lbaHi  = sataPassthrough.ata_cmd.endblk.chs.cyl_hi;
        scsiIoCtx->pAtaCmdOpts->rtfr.device = sataPassthrough.ata_cmd.endblk.chs.head;
    }
    // NOLINTEND(bugprone-branch-clone)
    // LBA mode, backwards compat bits, and dev bit should match what went in, so just take those from the tfr that
    // was sent -TJE NOTE: Does not appear to be a way to read lower 4 device/head bits after command completion
    // -TJE
    if (scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead & DEVICE_SELECT_BIT)
    {
        scsiIoCtx->pAtaCmdOpts->rtfr.device |= DEVICE_SELECT_BIT;
    }
    if (scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead & LBA_MODE_BIT)
    {
        scsiIoCtx->pAtaCmdOpts->rtfr.device |= LBA_MODE_BIT;
    }
    if (scsiIoCtx->pAtaCmdOpts->tfr.DeviceHead & DEVICE_REG_BACKWARDS_COMPATIBLE_BITS)
    {
        scsiIoCtx->pAtaCmdOpts->rtfr.device |= DEVICE_REG_BACKWARDS_COMPATIBLE_BITS;
    }

    // resid is another part of the structure. I'm guessing this is a count of what data was or was not transferred?
    // -TJE

    scsiIoCtx->device->drive_info.lastCommandTimeNanoSeconds = get_Nano_Seconds(commandTimer);
    return ret;
}

eReturnValues send_IO(ScsiIoCtx* scsiIoCtx)
{
    // switch based on value stored in os_info to define which passthrough interface to use to issue commands -TJE
    eReturnValues ret = SUCCESS;
    switch (scsiIoCtx->device->drive_info.interface_type)
    {
    case IDE_INTERFACE:
    case SCSI_INTERFACE:
        switch (scsiIoCtx->device->os_info.ptType)
        {
        case AIX_PASSTHROUGH_SCSI:
            // using only SCSI passthrough for now. We can use the
            // DIAG IO when the diagnostic flag was set on open, but that is not currently done today.-TJE
            // ret = send_AIX_SCSI_Diag_IO(scsiIoCtx);
            ret = send_AIX_SCSI_Passthrough(scsiIoCtx);
            break;
        case AIX_PASSTHROUGH_IDE_ATA:
            ret = send_AIX_IDE_ATA_Passthrough(scsiIoCtx);
            break;
        case AIX_PASSTHROUGH_IDE_ATAPI:
            ret = send_AIX_IDE_ATAPI_Passthrough(scsiIoCtx);
            break;
        case AIX_PASSTHROUGH_SATA:
            ret = send_AIX_SATA_Passthrough(scsiIoCtx);
            break;
        default:
            ret = OS_PASSTHROUGH_FAILURE;
            break;
        }
        break;
#if !defined(DISABLE_NVME_PASSTHROUGH)
    case NVME_INTERFACE:
        ret = sntl_Translate_SCSI_Command(scsiIoCtx->device, scsiIoCtx);
        break;
#endif // DISABLE_NVME_PASSTHROUGH
    case RAID_INTERFACE:
        if (scsiIoCtx->device->issue_io != M_NULLPTR)
        {
            ret = scsiIoCtx->device->issue_io(scsiIoCtx);
        }
        else
        {
            if (VERBOSITY_QUIET < scsiIoCtx->device->deviceVerbosity)
            {
                printf("No Raid PassThrough IO Routine present for this device\n");
            }
        }
        break;
    default:
        ret = OS_PASSTHROUGH_FAILURE;
        break;
    }
    if (scsiIoCtx->device->delay_io)
    {
        delay_Milliseconds(scsiIoCtx->device->delay_io);
        if (VERBOSITY_COMMAND_NAMES <= scsiIoCtx->device->deviceVerbosity)
        {
            printf("Delaying between commands %d seconds to reduce IO impact", scsiIoCtx->device->delay_io);
        }
    }
    return ret;
}

// TODO: Adjust filter to only get rhdisk# and not rhdiskl (or other letters?)
static int rhdisk_filter(const struct dirent* entry)
{
    return !strncmp("rhdisk", entry->d_name, 6);
}

// TODO: In a RAID configuration, physical disks in the RAID get a /dev/pdsk handle
//       Maybe this can be used to passthrough commands?
//       https://www.ibm.com/docs/en/power6?topic=srcao-disk-arrays

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
eReturnValues get_Device_Count(uint32_t* numberOfDevices, uint64_t flags)
{
    int             num_devs = 0;
    struct dirent** namelist;
    num_devs = scandir("/dev", &namelist, rhdisk_filter, alphasort);
    // free the list of names to not leak memory
    for (int iter = 0; iter < num_devs; ++iter)
    {
        safe_free_dirent(&namelist[iter]);
    }
    safe_free_dirent(M_REINTERPRET_CAST(struct dirent**, &namelist));

    *numberOfDevices = num_devs;

    M_USE_UNUSED(flags);
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
//!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
//
//-----------------------------------------------------------------------------
#define AIX_NAME_LEN 80
eReturnValues get_Device_List(tDevice* const ptrToDeviceList, uint32_t sizeInBytes, versionBlock ver, uint64_t flags)
{
    eReturnValues returnValue           = SUCCESS;
    uint32_t      numberOfDevices       = UINT32_C(0);
    uint32_t      driveNumber           = UINT32_C(0);
    uint32_t      found                 = UINT32_C(0);
    uint32_t      failedGetDeviceCount  = UINT32_C(0);
    uint32_t      permissionDeniedCount = UINT32_C(0);
    int           fd                    = -1;
    tDevice*      d                     = M_NULLPTR;
    DECLARE_ZERO_INIT_ARRAY(char, name, AIX_NAME_LEN); // Because get device needs char

    int             num_devs = 0;
    struct dirent** namelist;

    num_devs = scandir("/dev", &namelist, rhdisk_filter, alphasort);

    char** devs = M_REINTERPRET_CAST(char**, safe_calloc(num_devs + 1, sizeof(char*)));
    int    i    = 0;
    // add rhdisk devices to the list
    for (; i < (num_devs); i++)
    {
        size_t handleSize = (safe_strlen("/dev/") + safe_strlen(namelist[i]->d_name) + 1) * sizeof(char);
        devs[i]           = M_REINTERPRET_CAST(char*, safe_malloc(handleSize));
        snprintf_err_handle(devs[i], handleSize, "/dev/%s", namelist[i]->d_name);
        safe_free_dirent(&namelist[i]);
    }
    devs[i] = M_NULLPTR; // Added this so the for loop down doesn't cause a segmentation fault.
    safe_free_dirent(M_REINTERPRET_CAST(struct dirent**, &namelist));

    DISABLE_NONNULL_COMPARE
    if (ptrToDeviceList == M_NULLPTR || sizeInBytes == UINT32_C(0))
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
        d               = ptrToDeviceList;
        for (driveNumber = UINT32_C(0);
             ((driveNumber >= UINT32_C(0) && driveNumber < MAX_DEVICES_TO_SCAN && driveNumber < (num_devs)) &&
              (found < numberOfDevices));
             ++driveNumber)
        {
            if (!devs[driveNumber] || safe_strlen(devs[driveNumber]) == 0)
            {
                continue;
            }
            safe_memset(name, AIX_NAME_LEN, 0, AIX_NAME_LEN); // clear name before reusing it
            snprintf_err_handle(name, AIX_NAME_LEN, "%s", devs[driveNumber]);
            fd = -1;
            // lets try to open the device.
            // NOTE: When opening a handle, there may be an issue if SC_DIAGNOSTIC is not specified.
            // This can be an issue when there is somethign not quite right with the drive.
            // This cannot be used all the time though. You cannot use it on the system drive.
            // So first try without it, then try again if it won't open with SC_DIAGNOSTIC.
            long extensionFlag = 0L; // start with no additional flags - TJE
            bool opened        = false;
            fd                 = openx(name, 0, 0, extensionFlag);
            if (fd >= 0)
            {
                opened = true;
            }
            else
            {
                extensionFlag = SC_DIAGNOSTIC;
                fd            = openx(name, 0, 0, extensionFlag);
                if (fd >= 0)
                {
                    opened = true;
                }
            }
            if (opened)
            {
                close(fd);
                eVerbosityLevels temp = d->deviceVerbosity;
                safe_memset(d, sizeof(tDevice), 0, sizeof(tDevice));
                d->deviceVerbosity = temp;
                d->sanity.size     = ver.size;
                d->sanity.version  = ver.version;
                d->dFlags          = flags;
                eReturnValues ret  = get_Device(name, d);
                if (ret != SUCCESS)
                {
                    failedGetDeviceCount++;
                }
                found++;
                d++;
            }
            else if (errno == EACCES) // quick fix for opening drives without sudo
            {
                ++permissionDeniedCount;
                failedGetDeviceCount++;
            }
            else
            {
                failedGetDeviceCount++;
            }
            // free the dev[deviceNumber] since we are done with it now.
            safe_free(&devs[driveNumber]);
        }
        if (found == failedGetDeviceCount)
        {
            returnValue = FAILURE;
        }
        else if (permissionDeniedCount == (num_devs))
        {
            returnValue = PERMISSION_DENIED;
        }
        else if (failedGetDeviceCount && returnValue != PERMISSION_DENIED)
        {
            returnValue = WARN_NOT_ALL_DEVICES_ENUMERATED;
        }
    }
    RESTORE_NONNULL_COMPARE
    safe_free(M_REINTERPRET_CAST(void**, &devs));
    return returnValue;
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
eReturnValues close_Device(tDevice* dev)
{
    int retValue = 0;
    if (dev != M_NULLPTR)
    {
        retValue                = close(dev->os_info.fd);
        dev->os_info.last_error = errno;

        if (dev->os_info.ctrlfdValid)
        {
            if (close(dev->os_info.ctrlfd) == 0)
            {
                dev->os_info.ctrlfd = -1;
            }
        }

        if (retValue == 0)
        {
            dev->os_info.fd = -1;
            return SUCCESS;
        }
        else
        {
            return FAILURE;
        }
    }
    else
    {
        return MEMORY_FAILURE;
    }
}

eReturnValues send_NVMe_IO(nvmeCmdCtx* nvmeIoCtx)
{
#if !defined(DISABLE_NVME_PASSTHROUGH)
    // In AIX, you must issue Admin commands on the controller handle
    // NVM or other commands sets can be issued on the rhdisk handle
    // If this is not done properly, undefined behavior will occur as it may issue the wrong command on the wrong queue.
    eReturnValues        ret               = SUCCESS;
    int                  fdForNVMePassthru = nvmeIoCtx->commandType == NVM_ADMIN_CMD
                                                 ? nvmeIoCtx->device->os_info.ctrlfd
                                                 : nvmeIoCtx->device->os_info.fd; // start assuming rhdisk handle for now
    struct nvme_passthru nvmePassthrough;
    DECLARE_SEATIMER(commandTimer);
    safe_memset(&nvmePassthrough, sizeof(struct nvme_passthru), 0, sizeof(struct nvme_passthru));

    nvmePassthrough.version = 0; // version 0 is only version so far -TJE

    // TODO: Are read/write flags necessary???-TJE
    //  switch(nvmeIoCtx->commandDirection)
    //  {
    //  case XFER_NO_DATA:
    //  case XFER_DATA_IN:
    //      nvmePassthrough.flags |= B_READ;
    //      break;
    //  case XFER_DATA_OUT:
    //      nvmePassthrough.flags |= B_WRITE;
    //      break;
    //  case XFER_DATA_IN_OUT:
    //  case XFER_DATA_OUT_IN:
    //      nvmePassthrough.flags |= B_READ | B_WRITE;
    //      break;
    //  }

    // TODO: If cdw2 or cdw3 are set, need to return some kind of OS_COMMAND_NOT_SUPPORTED error

    // Setup the command. NOTE: this must be set in host endianness, not device endianness (little)
    if (nvmeIoCtx->commandType == NVM_ADMIN_CMD)
    {
        nvmePassthrough.cmd.opc      = nvmeIoCtx->cmd.adminCmd.opcode;
        nvmePassthrough.cmd.flag     = nvmeIoCtx->cmd.adminCmd.flags;
        nvmePassthrough.cmd.nsid     = nvmeIoCtx->cmd.adminCmd.nsid;
        nvmePassthrough.cmd.dword_10 = nvmeIoCtx->cmd.adminCmd.cdw10;
        nvmePassthrough.cmd.dword_11 = nvmeIoCtx->cmd.adminCmd.cdw11;
        nvmePassthrough.cmd.dword_12 = nvmeIoCtx->cmd.adminCmd.cdw12;
        nvmePassthrough.cmd.dword_13 = nvmeIoCtx->cmd.adminCmd.cdw13;
        nvmePassthrough.cmd.dword_14 = nvmeIoCtx->cmd.adminCmd.cdw14;
        nvmePassthrough.cmd.dword_15 = nvmeIoCtx->cmd.adminCmd.cdw15;
    }
    else
    {
        // assume nvm command set command for now - TJE
        nvmePassthrough.cmd.opc      = nvmeIoCtx->cmd.nvmCmd.opcode;
        nvmePassthrough.cmd.flag     = nvmeIoCtx->cmd.nvmCmd.flags;
        nvmePassthrough.cmd.nsid     = nvmeIoCtx->cmd.nvmCmd.nsid;
        nvmePassthrough.cmd.dword_10 = nvmeIoCtx->cmd.nvmCmd.cdw10;
        nvmePassthrough.cmd.dword_11 = nvmeIoCtx->cmd.nvmCmd.cdw11;
        nvmePassthrough.cmd.dword_12 = nvmeIoCtx->cmd.nvmCmd.cdw12;
        nvmePassthrough.cmd.dword_13 = nvmeIoCtx->cmd.nvmCmd.cdw13;
        nvmePassthrough.cmd.dword_14 = nvmeIoCtx->cmd.nvmCmd.cdw14;
        nvmePassthrough.cmd.dword_15 = nvmeIoCtx->cmd.nvmCmd.cdw15;
    }
    // set data length and datat pointer
    nvmePassthrough.cmd.data_length = nvmeIoCtx->dataSize;
    nvmePassthrough.cmd.data        = nvmeIoCtx->ptrData;
    // set the timeout
    if (nvmeIoCtx->device->drive_info.defaultTimeoutSeconds > 0 &&
        nvmeIoCtx->device->drive_info.defaultTimeoutSeconds > nvmeIoCtx->timeout)
    {
        nvmePassthrough.cmd.timeout = nvmeIoCtx->device->drive_info.defaultTimeoutSeconds;
        if (nvmeIoCtx->device->drive_info.defaultTimeoutSeconds >= AIX_MAX_CMD_TIMEOUT_SECONDS)
        {
            nvmePassthrough.cmd.timeout = UINT32_MAX; // no timeout or maximum timeout
        }
    }
    else
    {
        if (nvmeIoCtx->timeout != 0)
        {
            nvmePassthrough.cmd.timeout = nvmeIoCtx->timeout;
            if (nvmeIoCtx->device->drive_info.defaultTimeoutSeconds >= AIX_MAX_CMD_TIMEOUT_SECONDS)
            {
                nvmePassthrough.cmd.timeout = UINT32_MAX; // no timeout or maximum timeout
            }
        }
        else
        {
            nvmePassthrough.cmd.timeout = 15; // default to 15 second timeout
        }
    }

    // issue the IO
    start_Timer(&commandTimer);
    int ioctlResult = ioctl(fdForNVMePassthru, NVME_PASSTHRU, &nvmePassthrough);
    stop_Timer(&commandTimer);

    nvmeIoCtx->device->os_info.last_error = errno;
    if (ioctlResult < 0)
    {
        ret = OS_PASSTHROUGH_FAILURE;
        if (VERBOSITY_COMMAND_VERBOSE <= nvmeIoCtx->device->deviceVerbosity)
        {
            if (nvmeIoCtx->device->os_info.last_error != 0)
            {
                printf("Error: ");
                print_Errno_To_Screen(nvmeIoCtx->device->os_info.last_error);
            }
            if (nvmeIoCtx->device->os_info.last_error == EINVAL)
            {
                printf("Invalid field in nvme passthrough struct:\n")
                    // response status has a code set to help indicate what was not allowed
                    switch (nvmePassthrough->resp.status)
                {
                case NVME_PASSTHRU_INVAL_DATA_LENGTH:
                    printf("\tInvalid data length\n");
                    break;
                case NVME_PASSTHRU_BLOCKED_OP_CODE:
                    printf("\tOperation code blocked\n");
                    break;
                case NVME_PASSTHRU_CMD_HAS_NO_DATA:
                    printf("\tCommand has no data\n");
                    break;
                default:
                    printf("\tUnknown invalid field: %04" PRIX16 "\n", nvmePassthrough->resp.status);
                    break;
                }
            }
        }
        if (nvmeIoCtx->device->os_info.last_error == EINVAL)
        {
            // response status has a code set to help indicate what was not allowed
            switch (nvmePassthrough->resp.status)
            {
            case NVME_PASSTHRU_BLOCKED_OP_CODE:
                ret = OS_COMMAND_BLOCKED;
                break;
            case NVME_PASSTHRU_INVAL_DATA_LENGTH:
            case NVME_PASSTHRU_CMD_HAS_NO_DATA:
            default:
                ret = OS_PASSTHROUGH_FAILURE;
                break;
            }
        }
    }
    else // ret == 0. NOTE: Positive return values will currently fall here too but those are not documented as far as I
         // can see - TJE
    {
        ret = SUCCESS;
        // response contains the status code and dword 0
        nvmeIoCtx->commandCompletionData.dw0Valid = true;
        nvmeIoCtx->commandCompletionData.dw3Valid = true;
        nvmeIoCtx->commandCompletionData.dw0 =
            nvmePassthrough.resp.dword_0; // TODO: Is this returned in host endianness or does it need swapping?
        nvmeIoCtx->commandCompletionData.dw3 = M_WordsTo4ByteValue(
            nvmePassthrough.resp.status, 0); // phase is cleared, command identifier is not available
    }

    if (nvmeIoCtx->device->delay_io)
    {
        delay_Milliseconds(nvmeIoCtx->device->delay_io);
        if (VERBOSITY_COMMAND_NAMES <= nvmeIoCtx->device->deviceVerbosity)
        {
            printf("Delaying between commands %d seconds to reduce IO impact", nvmeIoCtx->device->delay_io);
        }
    }

    return ret;
#else
    M_USE_UNUSED(nvmeIoCtx);
    return OS_COMMAND_NOT_AVAILABLE;
#endif
}

eReturnValues os_nvme_Reset(tDevice* device)
{
#if !defined(DISABLE_NVME_PASSTHROUGH)
    eReturnValues    ret = SUCCESS;
    struct nvme_cntl nvmeReset;
    DECLARE_SEATIMER(commandTimer);
    safe_memset(&nvmeReset, sizeof(struct nvme_cntl), 0, sizeof(struct nvme_cntl));

    nvmeReset.version              = 0;
    nvmeReset.action               = NVME_RESET;
    nvmeReset.cmd.reset.reset_type = NVME_CTLR_RESET;
    start_Timer(&commandTimer);
    int ioctlResult = ioctl(nvmeIoCtx->device->os_info.ctrlfd, NVME_CNTL, &nvmeReset);
    stop_Timer(&commandTimer);

    nvmeIoCtx->device->os_info.last_error = errno;
    if (ioctlResult < 0)
    {
        ret = OS_PASSTHROUGH_FAILURE;
        if (nvmeIoCtx->device->os_info.last_error != 0)
        {
            printf("Error: ");
            print_Errno_To_Screen(nvmeIoCtx->device->os_info.last_error);
        }
    }
    else
    {
        ret = SUCCESS;
    }
    return ret;
#else  // DISABLE_NVME_PASSTHROUGH
    M_USE_UNUSED(device);
    return OS_COMMAND_NOT_AVAILABLE;
#endif // DISABLE_NVME_PASSTHROUGH
}

eReturnValues os_nvme_Subsystem_Reset(tDevice* device)
{
#if !defined(DISABLE_NVME_PASSTHROUGH)
    return OS_COMMAND_NOT_AVAILABLE;
#else  // DISABLE_NVME_PASSTHROUGH
    M_USE_UNUSED(device);
    return OS_COMMAND_NOT_AVAILABLE;
#endif // DISABLE_NVME_PASSTHROUGH
}

eReturnValues pci_Read_Bar_Reg(M_ATTR_UNUSED tDevice* device,
                               M_ATTR_UNUSED uint8_t* pData,
                               M_ATTR_UNUSED uint32_t dataSize)
{
#if !defined(DISABLE_NVME_PASSTHROUGH)
    return OS_COMMAND_NOT_AVAILABLE;
#else // DISABLE_NVME_PASSTHROUGH
    return OS_COMMAND_NOT_AVAILABLE;
#endif
}

// supposedly, when not in diagnostic mode, the read(), write(), lseek() can all be used.
// This is currently not needed though.
// Another thing we may want to implement here is the read/write ioctl codes that are available.
eReturnValues os_Read(M_ATTR_UNUSED tDevice* device,
                      M_ATTR_UNUSED uint64_t lba,
                      M_ATTR_UNUSED bool     forceUnitAccess,
                      M_ATTR_UNUSED uint8_t* ptrData,
                      M_ATTR_UNUSED uint32_t dataSize)
{
    return NOT_SUPPORTED;
}

eReturnValues os_Write(M_ATTR_UNUSED tDevice* device,
                       M_ATTR_UNUSED uint64_t lba,
                       M_ATTR_UNUSED bool     forceUnitAccess,
                       M_ATTR_UNUSED uint8_t* ptrData,
                       M_ATTR_UNUSED uint32_t dataSize)
{
    return NOT_SUPPORTED;
}

eReturnValues os_Verify(M_ATTR_UNUSED tDevice* device, M_ATTR_UNUSED uint64_t lba, M_ATTR_UNUSED uint32_t range)
{
    return NOT_SUPPORTED;
}

eReturnValues os_Flush(M_ATTR_UNUSED tDevice* device)
{
    return NOT_SUPPORTED;
}

// add SC_DIAGNOSTIC flag
eReturnValues os_Lock_Device(tDevice* device)
{
    eReturnValues ret = SUCCESS;
    if (!device->os_info.diagnosticModeFlagInUse)
    {
        close(device->os_info.fd); // this must be done first or the openx will fail!
        // try opening with the diagnostic flag.
        long extensionFlag = SC_DIAGNOSTIC;
        device->os_info.fd = openx(device->os_info.name, 0, 0, extensionFlag);
        if (device->os_info.fd >= 0)
        {
            device->os_info.diagnosticModeFlagInUse = true;
        }
        else
        {
            // reopen original fd without SC_DIAGNOSTIC
            extensionFlag      = 0;
            device->os_info.fd = openx(device->os_info.name, 0, 0, extensionFlag);
            ret                = FAILURE;
        }
    }
    return ret;
}

// remove SC_DIAGNOSTIC flag
eReturnValues os_Unlock_Device(tDevice* device)
{
    eReturnValues ret = SUCCESS;
    if (device->os_info.diagnosticModeFlagInUse)
    {
        close(device->os_info.fd); // this must be done first or the openx will fail!
        // try opening without the diagnostic flag.
        long extensionFlag = 0L;
        device->os_info.fd = openx(device->os_info.name, 0, 0, extensionFlag);
        if (device->os_info.fd >= 0)
        {
            device->os_info.diagnosticModeFlagInUse = false;
        }
        else
        {
            // reopen original fd without SC_DIAGNOSTIC
            extensionFlag      = SC_DIAGNOSTIC;
            device->os_info.fd = openx(device->os_info.name, 0, 0, extensionFlag);
            ret                = FAILURE;
        }
    }
    return ret;
}

// use mount/vmount with the remount option??? (see links below)
eReturnValues os_Update_File_System_Cache(M_ATTR_UNUSED tDevice* device)
{
    return NOT_SUPPORTED;
}

eReturnValues os_Erase_Boot_Sectors(M_ATTR_UNUSED tDevice* device)
{
    return NOT_SUPPORTED;
}

// etc/filesystems https://www.ibm.com/docs/en/aix/7.3?topic=files-filesystems-file
// https://www.ibm.com/docs/en/aix/7.3?topic=files-filsysh-file
// https://www.ibm.com/docs/en/aix/7.3?topic=files-fullstath-file
// https://www.ibm.com/docs/en/aix/7.3?topic=u-umount-uvmount-subroutine#umount
// https://www.ibm.com/docs/en/aix/7.3?topic=m-mntctl-subroutine
eReturnValues os_Unmount_File_Systems_On_Device(M_ATTR_UNUSED tDevice* device)
{
    return NOT_SUPPORTED;
}
