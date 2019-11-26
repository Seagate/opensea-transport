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
// \file common.h
// \brief Defines the constants structures and function headers that are common to OS & Non-OS code.


#pragma once

#include "common.h"
#include "version.h"
#if defined (VMK_CROSS_COMP)
#include "vm_nvme_lib.h"
#endif
#if defined (UEFI_C_SOURCE)
#include <Protocol/ScsiPassThruExt.h> //for TARGET_MAX_BYTES definition
#include <Protocol/DevicePath.h> //for device path union/structures
#endif

#if defined (__cplusplus)
#define __STDC_FORMAT_MACROS
extern "C"
{
#endif

    //This is a bunch of stuff for creating opensea-transport as a dynamic library (DLL in Windows or shared object in linux)
    #if defined(OPENSEA_TRANSPORT_API)
        #undef(OPENSEA_TRANSPORT_API)
    #endif

    #if defined(_WIN32) //DLL/LIB....be VERY careful making modifications to this unless you know what you are doing!
        #if defined (EXPORT_OPENSEA_TRANSPORT) && defined(STATIC_OPENSEA_TRANSPORT)
            #error "The preprocessor definitions EXPORT_OPENSEA_TRANSPORT and STATIC_OPENSEA_TRANSPORT cannot be combined!"
        #elif defined(STATIC_OPENSEA_TRANSPORT)
            #pragma message("Compiling opensea-transport as a static library!")
            #define OPENSEA_TRANSPORT_API
        #elif defined(EXPORT_OPENSEA_TRANSPORT)
            #pragma message("Compiling opensea-transport as exporting DLL!")
            #define OPENSEA_TRANSPORT_API __declspec(dllexport)
        #elif defined(IMPORT_OPENSEA_TRANSPORT)
            #pragma message("Compiling opensea-transport as importing DLL!")
            #define OPENSEA_TRANSPORT_API __declspec(dllimport)
        #else
            #error "You must specify STATIC_OPENSEA_TRANSPORT or EXPORT_OPENSEA_TRANSPORT or IMPORT_OPENSEA_TRANSPORT in the preprocessor definitions!"
        #endif
    #else //SO/A....as far as I know, nothing needs to be done here
        #define OPENSEA_TRANSPORT_API
    #endif

    #define SEAGATE_VENDOR_ID       (0x1BB1)

    #define OPENSEA_MAX_CONTROLLERS (8U)
    #define MAX_DEVICES_PER_CONTROLLER (256U)
    #define MAX_DEVICES_TO_SCAN (OPENSEA_MAX_CONTROLLERS * MAX_DEVICES_PER_CONTROLLER)

    #define SERIAL_NUM_LEN          (20) //Going with ATA lengths
    #define MODEL_NUM_LEN           (40)
    #define FW_REV_LEN              (10)
    #define T10_VENDOR_ID_LEN       (8)

    typedef struct _apiVersionInfo 
    {
        uint8_t majorVersion;
        uint8_t minorVersion;
        uint8_t patchVersion; 
        uint8_t reserved; 
    } apiVersionInfo;

// These need to be moved to ata_helper.h
    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    #pragma pack(push, 1)
    #endif
    typedef struct
    {
        union {
            uint16_t Config; //This has to be firs
            uint16_t Word000;
        };
        union {
            uint16_t DefCyln;
            uint16_t Word001;
        };
        union {
            uint16_t Resv1;
            uint16_t Word002;
        };
        union {
            uint16_t DefHead;
            uint16_t Word003;
        };
        uint16_t Word004;
        uint16_t Word005;
        union {
            uint16_t DefSector;
            uint16_t Word006;
        };
        uint16_t Word007;
        uint16_t Word008;
        uint16_t Word009;

        uint8_t  SerNum[20];

        uint16_t Word020;
        union {
            uint16_t BufSize;           // 21
            uint16_t Word021;
        };
        union {
            uint16_t RWLongByte;        // 22
            uint16_t Word022;
        };
        uint8_t  FirmVer[8];        // 23 24 25 26
        uint8_t  ModelNum[40];      // 27 ... 46
        union {
            uint8_t  BLK_SIZE[2];       // 47
            uint16_t Word047;
        };
        union {
            uint16_t DoubleWordIO;      // 48
            uint16_t Word048;
        };
        union {
            uint16_t Capability;        // 49
            uint16_t Word049;
        };

        uint16_t Word050;
        union {
            uint16_t PIOCycleTimeMode;  // 51
            uint16_t Word051;
        };
        union {
            uint16_t DMACycleTimeMode;  // 52
            uint16_t Word052;
        };
        union {
            uint16_t ValidWord;         // 53
            uint16_t Word053;
        };
        union {
            uint16_t CurCyln;           // 54
            uint16_t Word054;
        };
        union {
            uint16_t CurHead;           // 55
            uint16_t Word055;
        };
        union {
            uint16_t CurSector;         // 56
            uint16_t Word056;
        };
        uint32_t TotSectNumCHS;     // 57 58
        union {
            uint16_t MultiSectNum;      // 59
            uint16_t Word059;
        };

        uint32_t TotSectNumLBA;     // 60 61
        union {
            uint16_t SingleDMAMode;     // 62
            uint16_t Word062;
        };
        union {
            struct {
                uint8_t  MwDmaModesSupported;
                uint8_t  MultiDMAMode; // 63
            }mwdmaInfo;
            uint16_t Word063;
        };
        union {
            uint16_t AdvanPIOMode;      // 64
            uint16_t Word064;
        };
        union {
            uint16_t MinDMACycleTime;      // 65
            uint16_t Word065;
        };
        union {
            uint16_t RecDMACycleTime;      // 66
            uint16_t Word066;
        };
        union {
            uint16_t MinPIOCycleTime;      // 67
            uint16_t Word067;
        };
        union {
            uint16_t MinPIOCycleTimeIORDY; // 68
            uint16_t Word068;
        };
        uint16_t Word069;

        uint16_t Word070;
        uint16_t Word071;
        uint16_t Word072;
        uint16_t Word073;
        uint16_t Word074;
        union {
            uint16_t MaxQueueTag; // 75, bits 0-4 only
            uint16_t Word075;
        };
        union {
            uint16_t SataCapabilities; // 76
            uint16_t Word076;
        };
        uint16_t Word077;
        uint16_t Word078;
        uint16_t Word079;

        uint16_t Word080;
        uint16_t Word081;
        union {
            uint16_t CommandsAndFeaturesSupported1; // 82
            uint16_t Word082;
        };
        union {
            uint16_t CommandsAndFeaturesSupported2; // 83
            uint16_t Word083;
        };
        union {
            uint16_t CommandsAndFeaturesSupported3; // 84
            uint16_t Word084;
        };
        union {
            uint16_t CommandsAndFeaturesEnabled1;   // 85
            uint16_t Word085;
        };
        union {
            uint16_t CommandsAndFeaturesEnabled2;   // 86
            uint16_t Word086;
        };
        union {
            uint16_t CommandsAndFeaturesEnabled3;   // 87
            uint16_t Word087;
        };
        union {
            struct {
                uint8_t  UDmaModesSupported;
                uint8_t  UDmaMode; // 88
            }udmaModeInfo;
            uint16_t Word088;
        };
        uint16_t Word089;

        uint16_t Word090;
        uint16_t Word091;
        uint16_t Word092;
        uint16_t Word093;
        uint16_t Word094;
        uint16_t Word095;
        uint16_t Word096;
        uint16_t Word097;
        uint16_t Word098;
        uint16_t Word099;


        //Both Changed from tUINT32 to 16
        uint32_t TotSectNumLBALo;
        uint32_t TotSectNumLBAHi;

        uint16_t Word104;
        uint16_t Word105;
        //union {  // 106
        //   uint16_t AsWord;
        //   struct  {
        //      uint16_t LogicalSectorCount:4; // Bits 0-3
        //      uint16_t Reserved4_11:8;       // Bits 4-11
        //      uint16_t LargeSectors:1;       // Bit 12
        //      uint16_t LogicalSectors:1;     // Bit 13
        //      uint16_t FeatureSupport:2;     // Bits 14-15
        //   } bitfields;
        //} SectorSizeReport;
        // Commentedout the above because of portability issues with bit fields.
        union {
            uint16_t SectorSizeReport;
            uint16_t Word106;
        };
        uint16_t Word107;
        uint16_t Word108;
        uint16_t Word109;

        uint16_t Word110;
        uint16_t Word111;
        uint16_t Word112;
        uint16_t Word113;
        uint16_t Word114;
        uint16_t Word115;
        uint16_t Word116;
        uint16_t SectorSize[2]; // 117-118
        uint16_t Word119;

        uint16_t Word120;
        uint16_t Word121;
        uint16_t Word122;
        uint16_t Word123;
        uint16_t Word124;
        uint16_t Word125;
        uint16_t Word126;
        uint16_t Word127;
        uint16_t Word128;
        uint16_t Word129;

        uint16_t Word130;
        uint16_t Word131;
        uint16_t Word132;
        uint16_t Word133;
        uint16_t Word134;
        uint16_t Word135;
        uint16_t Word136;
        uint16_t Word137;
        uint16_t Word138;
        uint16_t Word139;

        uint16_t Word140;
        uint16_t Word141;
        uint16_t Word142;
        uint16_t Word143;
        uint16_t Word144;
        uint16_t Word145;
        uint16_t Word146;
        uint16_t Word147;
        uint16_t Word148;
        uint16_t Word149;

        union {
            uint16_t VendorUniqueTDSupport; // 150
            uint16_t Word150;
        };
        uint16_t Word151;
        uint16_t Word152;
        uint16_t Word153;
        uint16_t Word154;
        uint16_t Word155;
        uint16_t Word156;
        uint16_t Word157;
        uint16_t Word158;
        uint16_t Word159;

        uint16_t Word160;
        uint16_t Word161;
        uint16_t Word162;
        uint16_t Word163;
        uint16_t Word164;
        uint16_t Word165;
        uint16_t Word166;
        uint16_t Word167;
        uint16_t Word168;
        uint16_t Word169;

        uint16_t Word170;
        uint16_t Word171;
        uint16_t Word172;
        uint16_t Word173;
        uint16_t Word174;
        uint16_t Word175;
        uint16_t Word176;
        uint16_t Word177;
        uint16_t Word178;
        uint16_t Word179;

        uint16_t Word180;
        uint16_t Word181;
        uint16_t Word182;
        uint16_t Word183;
        uint16_t Word184;
        uint16_t Word185;
        uint16_t Word186;
        uint16_t Word187;
        uint16_t Word188;
        uint16_t Word189;

        uint16_t Word190;
        uint16_t Word191;
        uint16_t Word192;
        uint16_t Word193;
        uint16_t Word194;
        uint16_t Word195;
        uint16_t Word196;
        uint16_t Word197;
        uint16_t Word198;
        uint16_t Word199;

        uint16_t Word200;
        uint16_t Word201;
        uint16_t Word202;
        uint16_t Word203;
        uint16_t Word204;
        uint16_t Word205;
        uint16_t Word206;
        uint16_t Word207;
        uint16_t Word208;
        uint16_t Word209;

        uint16_t Word210;
        uint16_t Word211;
        uint16_t Word212;
        uint16_t Word213;
        uint16_t Word214;
        uint16_t Word215;
        uint16_t Word216;
        uint16_t Word217;
        uint16_t Word218;
        uint16_t Word219;

        uint16_t Word220;
        uint16_t Word221;
        uint16_t Word222;
        uint16_t Word223;
        uint16_t Word224;
        uint16_t Word225;
        uint16_t Word226;
        uint16_t Word227;
        uint16_t Word228;
        uint16_t Word229;

        uint16_t Word230;
        uint16_t Word231;
        uint16_t Word232;
        uint16_t Word233;
        uint16_t Word234;
        uint16_t Word235;
        uint16_t Word236;
        uint16_t Word237;
        uint16_t Word238;
        uint16_t Word239;

        uint16_t Word240;
        uint16_t Word241;
        uint16_t Word242;
        uint16_t Word243;
        uint16_t Word244;
        uint16_t Word245;
        uint16_t Word246;
        uint16_t Word247;
        uint16_t Word248;
        uint16_t Word249;

        uint16_t Word250;
        uint16_t Word251;
        uint16_t Word252;
        uint16_t Word253;
        uint16_t Word254;
        uint16_t Word255;
    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    }tAtaIdentifyData, *ptAtaIdentifyData;
    #pragma pack(pop)
    #else
    }__attribute__((packed,aligned(1))) tAtaIdentifyData, *ptAtaIdentifyData;
    #endif


    #if !defined(DISABLE_NVME_PASSTHROUGH)
    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    #pragma pack(push,1)
    #endif
    //All of the NVME structs in here were moved here to fix a circular include issue
    typedef struct _nvmeIDPowerState {
        uint16_t            maxPower;   /* centiwatts */
        uint8_t             rsvd2;
        uint8_t             flags;
        uint32_t            entryLat;   /* microseconds */
        uint32_t            exitLat;    /* microseconds */
        uint8_t             readTPut;
        uint8_t             readLat;
        uint8_t             writeLput;
        uint8_t             writeLat;
        uint16_t            idlePower;
        uint8_t             idleScale;
        uint8_t             rsvd19;
        uint16_t            activePower;
        uint8_t             activeWorkScale;
        uint8_t             rsvd23[9];
    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    }nvmeIDPowerState;
    #pragma pack(pop)
    #else
    }__attribute__((packed,aligned(1))) nvmeIDPowerState;
    #endif

    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    #pragma pack(push,1)
    #endif
    typedef struct _nvmeIDCtrl {
        //controller capabilities and features
        uint16_t            vid;
        uint16_t            ssvid;
        char                sn[20];
        char                mn[40];
        char                fr[8];
        uint8_t             rab;
        uint8_t             ieee[3];
        uint8_t             cmic;
        uint8_t             mdts;
        uint16_t            cntlid;
        uint32_t            ver;
        uint32_t            rtd3r;
        uint32_t            rtd3e;
        uint32_t            oaes;
        uint32_t            ctratt;
        uint8_t             reservedBytes111_100[12];
        uint8_t             fguid[16];//128bit identifier
        uint8_t             reservedBytes239_128[112];
        uint8_t             nvmManagement[16];
        //Admin command set attribues & optional controller capabilities
        uint16_t            oacs;
        uint8_t             acl;
        uint8_t             aerl;
        uint8_t             frmw;
        uint8_t             lpa;
        uint8_t             elpe;
        uint8_t             npss;
        uint8_t             avscc;
        uint8_t             apsta;
        uint16_t            wctemp;
        uint16_t            cctemp;
        uint16_t            mtfa;
        uint32_t            hmpre;
        uint32_t            hmmin;
        uint8_t             tnvmcap[16];
        uint8_t             unvmcap[16];
        uint32_t            rpmbs;
        uint16_t            edstt;
        uint8_t             dsto;
        uint8_t             fwug;
        uint16_t            kas;
        uint16_t            hctma;
        uint16_t            mntmt;
        uint16_t            mxtmt;
        uint32_t            sanicap;
        uint8_t             reservedBytes511_332[180];
        //NVM command set attributes;
        uint8_t             sqes;
        uint8_t             cqes;
        uint16_t            maxcmd;
        uint32_t            nn;
        uint16_t            oncs;
        uint16_t            fuses;
        uint8_t             fna;
        uint8_t             vwc;
        uint16_t            awun;
        uint16_t            awupf;
        uint8_t             nvscc;
        uint8_t             reservedByte531;
        uint16_t            acwu;
        uint8_t             rsvd534[2];
        uint32_t            sgls;
        uint8_t             reservedBytes767_540[228];
        char                subnqn[256];
        uint8_t             reservedBytes1791_1024[768];
        uint8_t             nvmeOverFabrics[256];
        nvmeIDPowerState    psd[32];
        uint8_t             vs[1024];
    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    }nvmeIDCtrl;
    #pragma pack(pop)
    #else
    }__attribute__((packed,aligned(1))) nvmeIDCtrl;
    #endif

    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    #pragma pack(push,1)
    #endif
    typedef struct _nvmeLBAF {
        uint16_t            ms;
        uint8_t             lbaDS;
        uint8_t             rp;
    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    }nvmeLBAF;
    #pragma pack(pop)
    #else
    }__attribute__((packed,aligned(1))) nvmeLBAF;
    #endif

    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    #pragma pack(push,1)
    #endif
    typedef struct _nvmeIDNameSpaces {
        uint64_t            nsze;
        uint64_t            ncap;
        uint64_t            nuse;
        uint8_t             nsfeat;
        uint8_t             nlbaf;
        uint8_t             flbas;
        uint8_t             mc;
        uint8_t             dpc;
        uint8_t             dps;
        uint8_t             nmic;
        uint8_t             rescap;
        uint8_t             fpi;
        uint8_t             dlfeat;
        uint16_t            nawun;
        uint16_t            nawupf;
        uint16_t            nacwu;
        uint16_t            nabsn;
        uint16_t            nabo;
        uint16_t            nabspf;
        uint16_t            noiob;
        uint8_t             nvmcap[16];//128bit number
        uint8_t             rsvd40[40];//bytes 103:64
        uint8_t             nguid[16];
        uint8_t             eui64[8];
        nvmeLBAF            lbaf[16];
        uint8_t             rsvd192[192];
        uint8_t             vs[3712];
    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    }nvmeIDNameSpaces;
    #pragma pack(pop)
    #else
    }__attribute__((packed,aligned(1))) nvmeIDNameSpaces;
    #endif

    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    #pragma pack(push,1)
    #endif
    typedef struct _nvmeIdentifyData {
        nvmeIDCtrl          ctrl;
        nvmeIDNameSpaces    ns; // Currently we only support 1 NS - Revisit.  
    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    }nvmeIdentifyData;
    #pragma pack(pop)
    #else
    }__attribute__((packed,aligned(1))) nvmeIdentifyData;
    #endif

    #endif //disable NVME passthrough

    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    #pragma pack(push,1)
    #endif
    typedef struct _ataReturnTFRs
    {
        uint8_t                error;
        uint8_t                secCntExt;
        uint8_t                secCnt;
        uint8_t                lbaLowExt;
        uint8_t                lbaLow;
        uint8_t                lbaMidExt;
        uint8_t                lbaMid;
        uint8_t                lbaHiExt;
        uint8_t                lbaHi;
        uint8_t                device;
        uint8_t                status;
        uint8_t                padding[5];//empty padding to make sure this structure endds on an 8byte aligned boundary
    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    }ataReturnTFRs;
    #pragma pack(pop)
    #else
    }__attribute__((packed,aligned(1))) ataReturnTFRs;
    #endif

    // Defined by SPC3 as the maximum sense length
    #define SPC3_SENSE_LEN  UINT8_C(252)

    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    #pragma pack(push,1)
    #endif
    typedef struct _tVpdData {
        uint8_t  inquiryData[96]; //INQ_RETURN_DATA_LENGTH
        uint8_t  vpdPage83[64];
    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    }tVpdData;
    #pragma pack(pop)
    #else
    }__attribute__((packed,aligned(1))) tVpdData;
    #endif

    typedef enum _eMediaType {
        MEDIA_HDD       = 0,    // rotating media, HDD (ata or scsi)
        MEDIA_SSD       = 1,    // SSD (ata or scsi or nvme)
        MEDIA_SSM_FLASH = 2,    // Solid state flash module - USB flash drive/thumb drive
        MEDIA_SSHD      = 3,    // Hybrid drive.
        MEDIA_OPTICAL   = 4,    // CD/DVD/etc drive media
        MEDIA_TAPE      = 5,    // Tape Drive media
        MEDIA_NVM       = 6,    // All NVM drives
        MEDIA_UNKNOWN           // anything else we find should get this
    } eMediaType;

    typedef enum _eDriveType {
        UNKNOWN_DRIVE,
        ATA_DRIVE,
        SCSI_DRIVE,
        RAID_DRIVE,
        NVME_DRIVE,
        ATAPI_DRIVE,
        FLASH_DRIVE,//This is a USB thumb drive/flash drive or an SD card, or compact flash, etc
        LEGACY_TAPE_DRIVE//not currently used...
    } eDriveType;

    typedef enum _eInterfaceType {
        UNKNOWN_INTERFACE,
        IDE_INTERFACE,
        SCSI_INTERFACE,
        RAID_INTERFACE,
        NVME_INTERFACE,
        USB_INTERFACE,
        MMC_INTERFACE,
        SD_INTERFACE,
        IEEE_1394_INTERFACE
    } eInterfaceType;

    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    #pragma pack(push,1)
    #endif
    //revisit this later as this may not be the best way we want to do this
    typedef struct _bridgeInfo
    {
        bool isValid;
        uint16_t childSectorAlignment;//This will usually be set to 0 on newer drives. Older drives may set this alignment differently
        uint8_t padd0[5];
        char childDriveMN[MODEL_NUM_LEN + 1];
        uint8_t padd1[7];
        char childDriveSN[SERIAL_NUM_LEN + 1];
        uint8_t padd2[3];
        char childDriveFW[FW_REV_LEN + 1];
        uint8_t padd3[5];
        uint64_t childWWN;
        char t10SATvendorID[9];//VPD page 89h
        uint8_t padd4[7];
        char SATproductID[17];//VPD page 89h
        uint8_t padd5[7];
        char SATfwRev[9];//VPD page 89h
        uint8_t padd6[7];
        uint32_t childDeviceBlockSize; //This is the logical block size reported by the drive
        uint32_t childDevicePhyBlockSize; // This is the physical block size reported by the drive.
        uint64_t childDeviceMaxLba;
    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    }bridgeInfo;
    #pragma pack(pop)
    #else
    }__attribute__((packed,aligned(1))) bridgeInfo;
    #endif

    typedef enum _eAdapterInfoType
    {
        ADAPTER_INFO_UNKNOWN, //unknown generally means it is not valid or present and was not discovred by low-level OS code
        ADAPTER_INFO_USB,
        ADAPTER_INFO_PCI,
        ADAPTER_INFO_IEEE1394, //not supported yet...
    }eAdapterInfoType;

#if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
#pragma pack(push,1)
#endif
    //this structure may or may not be populated with some low-level device adapter info. This will hold USB or PCI/PCIe vendor, product, and revision codes which may help filter capabilities.
    typedef struct _adapterInfo
    {
        bool vendorIDValid;
        bool productIDValid;
        bool revisionValid;
        uint8_t padd[1];
        eAdapterInfoType infoType;
        //These may change sizes if we encounter other interfaces that report these are larger than uint16_t's
        uint16_t vendorID;
        uint16_t productID;
        uint16_t revision;
        uint16_t reserved;
#if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    }adapterInfo;
#pragma pack(pop)
#else
    }__attribute__((packed, aligned(1))) adapterInfo;
#endif


    typedef enum _eATASynchronousDMAMode
    {
        ATA_DMA_MODE_NO_DMA,
        ATA_DMA_MODE_DMA,
        ATA_DMA_MODE_MWDMA,
        ATA_DMA_MODE_UDMA
    }eATASynchronousDMAMode;

    typedef enum _ePassthroughType
    {
        ATA_PASSTHROUGH_SAT = 0,//Should be used unless you know EXACTLY the right pass through to use for a device.
        //All values below are for legacy USB support. They should only be used when you know what you are doing.
        ATA_PASSTHROUGH_CYPRESS,
        ATA_PASSTHROUGH_PROLIFIC,
        ATA_PASSTHROUGH_TI,
        ATA_PASSTHROUGH_NEC,
        ATA_PASSTHROUGH_PSP, //Some PSP drives use this passthrough and others use SAT...it's not clear if this was ever even used. If testing for it, test it last.
        ATA_PASSTHROUGH_UNKNOWN = 99,//final value to be used by ATA passthrough types
        //NVMe stuff defined here. All NVMe stuff should be 100 or higher with the exception of the default system passthrough
        NVME_PASSTHROUGH_SYSTEM = 0,//This is for NVMe devices to use the system passthrough. This is the default since this is most NVMe devices.
        NVME_PASSTHROUGH_JMICRON = 100,
        //TODO: Other vendor unique SCSI to NVMe passthrough here
        NVME_PASSTHROUGH_UNKNOWN,
        //No passthrough
        PASSTHROUGH_NONE = UINT32_MAX
    }ePassthroughType;

    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    #pragma pack(push,1)
    #endif
    typedef struct _ataOptions
    {
        eATASynchronousDMAMode dmaMode;
        bool dmaSupported;
        bool readLogWriteLogDMASupported;
        bool readBufferDMASupported;
        bool writeBufferDMASupported;
        bool downloadMicrocodeDMASupported;
        bool taggedCommandQueuingSupported;
        bool nativeCommandQueuingSupported;
        bool readWriteMultipleSupported;
        uint8_t logicalSectorsPerDRQDataBlock;
        bool isParallelTransport;
        bool isDevice1;//Don't rely on this. Only here for some OS's/passthroughs. Most shouldn't need this. SAT or the OS's passthrough will ignore this bit in the commands anyways.
        bool chsModeOnly;//AKA LBA not supported. Only really REALLY old drives should set this.
        bool writeUncorrectableExtSupported;
        bool fourtyEightBitAddressFeatureSetSupported;
        bool generalPurposeLoggingSupported;
        bool alwaysCheckConditionAvailableBit;//this will cause all commands to set the check condition bit. This means any ATA Passthrough command should always get back an ATA status which may help with sense data and judging what went wrong better. Be aware that this may not be liked on some devices and some may just ignore it.
        bool enableLegacyPassthroughDetectionThroughTrialAndError;//This must be set to true in order to work on legacy (ancient) passthrough if the VID/PID is not in the list and not read from the system.
        bool senseDataReportingEnabled;//this is to track when the RTFRs may contain a sense data bit so it can be read automatically.
        uint8_t padd[1];
    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    }ataOptions;
    #pragma pack(pop)
    #else
    }__attribute__((packed,aligned(1))) ataOptions;
    #endif

    typedef enum _eZonedDeviceType {
        ZONED_TYPE_NOT_ZONED = 0,
        ZONED_TYPE_HOST_AWARE = 1,
        ZONED_TYPE_DEVICE_MANAGED = 2,
        ZONED_TYPE_RESERVED = 3,
        ZONED_TYPE_HOST_MANAGED = 4
    }eZonedDeviceType;

    //This is used by the software SAT translation layer. DO NOT Update this directly
    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    #pragma pack(push,1)
    #endif
    typedef struct _softwareSATFlags
    {
        bool identifyDeviceDataLogSupported;
        bool deviceStatisticsSupported; //if set to true, any 1 of the bools in the following struct is set to true (supported)
        struct
        {
            bool rotatingMediaStatisticsPageSupported;
            bool generalErrorStatisticsSupported;
            bool temperatureStatisticsSupported;
            bool solidStateDeviceStatisticsSupported;
            bool generalStatisitcsSupported;
            bool dateAndTimeTimestampSupported;//on general statistics page
        }deviceStatsPages;
        bool currentInternalStatusLogSupported;//Needed for Error history mode of the read buffer command
        bool savedInternalStatusLogSupported;//Needed for Error history mode of the read buffer command
        bool deferredDownloadSupported;//Read from the identify device data log
        bool hostLogsSupported;//log addresses 80h - 9Fh
        bool senseDataDescriptorFormat;//DO NOT SET DIRECTLY! This should be changed through a mode select command to the software SAT layer. false = fixed format, true = descriptor format
        bool dataSetManagementXLSupported;//Needed to help the translator know when this command is supported so it can be used.
        bool zeroExtSupported;
        uint8_t rtfrIndex;
        ataReturnTFRs ataPassthroughResults[16];
    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    }softwareSATFlags;
    #pragma pack(pop)
    #else
    }__attribute__((packed,aligned(1))) softwareSATFlags;
    #endif

    //This is for test unit ready after failures to keep up performance on devices that slow down a LOT durring error processing (USB mostly)
    #define TURF_LIMIT 3

    //The passthroughHacks structure is to hold information to help with passthrough on OSs, USB adapters, SCSI adapters, etc. Most of this is related to USB adapters though.
    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    #pragma pack(push,1)
    #endif
    typedef struct _passthroughHacks
    {
        //generic information up top.
        bool hacksSetByReportedID;//This is true if the code was able to read and set hacks based on reported vendor and product IDs from lower levels. If this is NOT set, then the information below is set either by trial and error or by known product identification matches.
        bool someHacksSetByOSDiscovery;//Will be set if any of the below are set by default by the OS level code. This may happen in Windows for ATA/SCSI passthrough to ATA devices
        ePassthroughType passthroughType;//This should be left alone unless you know for a fact which passthrough to use. SAT is the default and should be used unless you know you need a legacy (pre-SAT) passthrough type.
        bool testUnitReadyAfterAnyCommandFailure;//This should be done whenever we have a device that is known to increase time to return response to bad commands. Many USB bridges need this.
        uint8_t turfValue;//This holds the number of times longer it takes a device to respond without test unit ready. This is held here to make it easier to change library wide without retesting a device.
        //SCSI hacks are those that relate to things to handle when issuing SCSI commands that may be translated improperly in some cases.
        struct
        {
            bool unitSNAvailable;//This means we can request this page even if other VPD pages don't work.
            struct {
                bool available;//means that the bools below have been set. If not, need to use default read/write settings in the library.
                bool rw6;
                bool rw10;
                bool rw12;
                bool rw16;
            }readWrite;
            bool noVPDPages;//no VPD pages are supported. The ONLY excetion to this is the unitSNAvailable bit above. Numerous USBs tested will only support that page...not even the list of pages will be supported by them.
            bool noModePages;//no mode pages are supported
            bool noLogPages;//no mode pages are supported
            bool noLogSupPages;
            bool mode6bytes;//mode sense/select 6 byte commands only
            bool noModeSubPages;//Subpages are not supported, don't try sending these commands
            bool noReportSupportedOperations;//report supported operation codes command is not supported.
            bool reportSingleOpCodes;//reporting supported operation codes specifying a specific operation code is supported by the device.
            bool reportAllOpCodes;//reporting all operation codes is supported by the device.
            bool securityProtocolSupported;//SCSI security protocol commands are supported
            bool securityProtocolWithInc512;//SCSI security protocol commands are ONLY supported with the INC512 bit set.
            uint32_t maxTransferLength;//Maximum SCSI command transfer length in bytes. Mostly here for USB where translations aren't accurate or don't show this properly.
            bool preSCSI2InqData;//If this is true, then the struct below is intended to specify where, and how long, the fields are for product ID, vendorID, revision, etc. This structure will likely need multiple changes as these old devices are encountered and work is done to support them - TJE
            struct {
                uint8_t productIDOffset;//If 0, not valid or reported
                uint8_t productIDLength;//If 0, not valid or reported
                uint8_t productRevOffset;
                uint8_t productRevLength;
                uint8_t vendorIDOffset;
                uint8_t vendorIDLength;
                uint8_t serialNumberOffset;
                uint8_t serialNumberLength;
            }scsiInq;
        }scsiHacks;
        //ATA Hacks refer to SAT translation issues or workarounds.
        struct {
            bool smartCommandTransportWithSMARTLogCommandsOnly;//for USB adapters that hang when sent a GPL command to SCT logs, but work fine with SMART log commands
            bool useA1SATPassthroughWheneverPossible;//For USB adapters that will only process 28bit commands with A1 and will NOT issue them with 85h
            bool a1NeverSupported;//prevent retrying with 12B command since it isn't supported anyways.
            bool returnResponseInfoSupported;//can send the SAT command to get response information for RTFRs
            bool returnResponseInfoNeedsTDIR;//supports return response info, but must have T_DIR bit set for it to work properly
            bool returnResponseIgnoreExtendBit;//Some devices support returning response info, but don't properly set the extend bit, so this basically means copy extended RTFRs anyways.
            bool alwaysUseTPSIUForSATPassthrough;//some USBs do this better than others.
            bool alwaysCheckConditionAvailable;//Not supported by all SAT translators. Don't set unless you know for sure!!!
            bool alwaysUseDMAInsteadOfUDMA;//send commands with DMA mode instead of UDMA since the device doesn't support UDMA passthrough modes.
            bool dmaNotSupported;//DMA passthrough is not available of any kind.
            bool partialRTFRs;//This means only 28bit RTFRs will be able to be retrived by the device. This hack is more helpful for code trying different commands to filter capabilities than for trying to talk to the device.
            bool noRTFRsPossible;//This means on command responses, we cannot get any return task file registers back from the device, so avoid commands that rely on this behavior
            bool multiSectorPIOWithMultipleMode;//This means that multisector PIO works, BUT only when a set multiple mode command has been sent first and it is limited to the multiple mode.
            bool singleSectorPIOOnly;//This means that the adapter only supports single sector PIO transfers
            bool ata28BitOnly;//This is for some devices where the passthrough only allows a 28bit command through, even if the target drive is 48bit
            uint32_t maxTransferLength;//ATA Passthrough max transfer length in bytes. This may be different than the scsi translation max.
        }ataPTHacks;
        //TODO: Add more hacks and padd this structure
        uint8_t padd[3];
    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    }passthroughHacks;
    #pragma pack(pop)
    #else
    }__attribute__((packed,aligned(1))) passthroughHacks;
    #endif

    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    #pragma pack(push,1)
    #endif
    typedef struct _driveInfo {
        eMediaType     media_type;
        eDriveType     drive_type;
        eInterfaceType interface_type;
        eZonedDeviceType zonedType;//most drives will report ZONED_TYPE_NOT_ZONED
        uint32_t       deviceBlockSize; //This is the logical block size reported by the drive
        uint32_t       devicePhyBlockSize; // This is the physical block size reported by the drive.
        uint32_t       dataTransferSize;//this the block size that will be transfered
        uint16_t       sectorAlignment;//This will usually be set to 0 on newer drives. Older drives may set this alignment differently
        uint8_t padd0[2];
        uint64_t       deviceMaxLba;
        char           serialNumber[SERIAL_NUM_LEN + 1];
        uint8_t padd1[7];
        char           T10_vendor_ident[T10_VENDOR_ID_LEN + 1];
        uint8_t padd2[7];
        char           product_identification[MODEL_NUM_LEN + 1]; //not INQ
        uint8_t padd3[7];
        char           product_revision[FW_REV_LEN + 1];
        uint8_t padd4[5];
        uint64_t       worldWideName;
        union{
            tAtaIdentifyData ata;
#if !defined(DISABLE_NVME_PASSTHROUGH)
            nvmeIdentifyData nvme;
#endif
            //reserved field below is set to 8192 because nvmeIdentifyData structure holds both controller and namespace data which are 4k each
            uint8_t reserved[8192];//putting this here to allow some compatibility when NVMe passthrough is NOT enabled.
        }IdentifyData;
        tVpdData         scsiVpdData; // Intentionally not part of the above IdentifyData union
        ataReturnTFRs lastCommandRTFRs;//This holds the RTFRs for the last command to be sent to the device. This is not necessarily the last function called as functions may send multiple commands to the device.
        struct {
            bool validData;//must be true for any other fields to be useful
            uint8_t senseKey;
            uint8_t additionalSenseCode;
            uint8_t additionalSenseCodeQualifier;
            uint8_t padd[4];
        }ataSenseData;
        uint8_t lastCommandSenseData[SPC3_SENSE_LEN];//This holds the sense data for the last command to be sent to the device. This is not necessarily the last function called as functions may send multiple commands to the device.
        uint8_t padd5[4];
        struct {
            uint32_t lastNVMeCommandSpecific;//DW0 of command completion. Not all OS's return this so it is not always valid...only really useful for SNTL when it is used. Linux, Solaris, FreeBSD, UEFI. Windows is the problem child here.
            uint32_t lastNVMeStatus;//DW3 of command completion. Not all OS's return this so it is not always valid...only really useful for SNTL when it is used. Linux, Solaris, FreeBSD, UEFI. Windows is the problem child here.
        }lastNVMeResult;
        //TODO: a union or something so that we don't need to keep adding more bytes for drive types that won't use the ATA stuff or NVMe stuff in this struct.
        bridgeInfo      bridge_info;
        adapterInfo     adapter_info;
        ataOptions      ata_Options;
        uint64_t        lastCommandTimeNanoSeconds;//The time the last command took in nanoseconds
        softwareSATFlags softSATFlags;//This is used by the software SAT translation layer. DO NOT Update this directly. This should only be updated by the lower layers of opensea-transport.
        uint32_t defaultTimeoutSeconds;//If this is not set (set to zero), a default value of 15 seconds will be used.
        uint8_t padd6[4];
        union {
            uint32_t namespaceID;//This is the current namespace you are talking with. If this is zero, then this value is invalid. This may not be available on all OS's or driver interfaces
            uint32_t lun;//Logical unit number for SCSI. Not currently populated.
        };
        uint8_t currentProtectionType;//Useful for certain operations. Read in readCapacityOnSCSI. TODO: NVMe
        uint8_t piExponent;//Only valid for protection types 2 & 3 I believe...-TJE
        uint8_t scsiVersion;//from STD Inquiry. Can be used elsewhere to help filter capabilities. NOTE: not an exact copy for old products where there was also EMCA and ISO versions. Set to ANSI version number in those cases.
        uint8_t padd7[4];//padd to 9304 bytes to make divisible by 8
        passthroughHacks passThroughHacks;
    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    }driveInfo;
    #pragma pack(pop)
    #else
    }__attribute__((packed,aligned(1))) driveInfo;
    #endif

#if defined(UEFI_C_SOURCE)
    typedef enum _eUEFIPassthroughType
    {
        UEFI_PASSTHROUGH_UNKNOWN,
        UEFI_PASSTHROUGH_SCSI,
        UEFI_PASSTHROUGH_SCSI_EXT,
        UEFI_PASSTHROUGH_ATA,
#if !defined (DISABLE_NVME_PASSTHROUGH)
        UEFI_PASSTHROUGH_NVME,
#endif
    }eUEFIPassthroughType;
#endif

#if defined (_WIN32) && !defined(UEFI_C_SOURCE)
    //TODO: see if we can move these WIndows specific enums out to the windows unique files.
    typedef enum _eWindowsIOCTLType
    {
        WIN_IOCTL_NOT_SET = 0,//this should only be like this when allocating memory...
        WIN_IOCTL_ATA_PASSTHROUGH,//Set when using ATA Pass-through IOCTLs (direct and double buffered)
        WIN_IOCTL_SCSI_PASSTHROUGH,//Set when using SCSI Pass-through IOCTLs (direct and double buffered)
        WIN_IOCTL_SCSI_PASSTHROUGH_EX,//Set when using SCSI Pass-through IOCTLs (direct and double buffered) //Win 8 and newer only! You can set this to force using the EX passthrough in Win8, BUT not many drivers support this and it will automatically be used for 32 byte CDBs when supported...that's the only real gain right now. - TJE
        WIN_IOCTL_SMART_ONLY,//Only the legacy SMART command IOCTL is supported, so only ATA identify and SMART commands are available
        WIN_IOCTL_IDE_PASSTHROUGH_ONLY,//Only the old & undocumented IDE pass-through is supported. Do not use this unless absolutely nothing else works.
        WIN_IOCTL_SMART_AND_IDE,//Only the legacy SMART and IDE IOCTLs are supported, so 28bit limitations abound
        WIN_IOCTL_STORAGE_PROTOCOL_COMMAND, //Win10 + only. Should be used with NVMe. Might work with SCSI or ATA, but that is unknown...development hasn't started for this yet. Just a placeholder - TJE
    }eWindowsIOCTLType;

    typedef enum _eWindowsIOCTLMethod
    {
        WIN_IOCTL_DEFAULT_METHOD,
        WIN_IOCTL_FORCE_ALWAYS_DIRECT,
        WIN_IOCTL_FORCE_ALWAYS_DOUBLE_BUFFERED,
        WIN_IOCTL_MAX_METHOD
    }eWindowsIOCTLMethod;
#endif
    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    #pragma pack(push,1)
    #endif
    // \struct typedef struct _OSDriveInfo
    typedef struct _OSDriveInfo
    {
        char                name[256];//handle name (string)
        char                friendlyName[24];//Handle name in a shorter/more friendly format. Example: name=\\.\PHYSICALDRIVE0 friendlyName=PD0
        eOSType             osType;//useful for lower layers to do OS specific things
        uint8_t             minimumAlignment;//This is a power of 2 value representing the byte alignment required. 0 - no requirement, 1 - single byte alignment, 2 - word, 4 - dword, 8 - qword, 16 - 128bit aligned
        uint8_t padd0[3];
        #if defined (UEFI_C_SOURCE)
        EFI_HANDLE          fd;
        EFI_DEV_PATH devicePath;//This type being used is a union of all the different possible device paths. - This is 48 bytes
        eUEFIPassthroughType passthroughType;
        union _uefiAddress {
            struct _scsiAddress{
                uint32_t target;
                uint64_t lun;
            }scsi;
            struct _scsiExtAddress{
                uint8_t target[TARGET_MAX_BYTES];
                uint64_t lun;
            }scsiEx;
            struct _ataAddress{
                uint16_t port;
                uint16_t portMultiplierPort;
            }ata;
            #if !defined (DISABLE_NVME_PASSTHROUGH)
            struct _nvmeAddress{
                uint32_t namespaceID;
            }nvme;
            #endif
            uint8_t raw[24];
        }address;
        uint16_t            controllerNum;//used to figure out which controller the above address applies to.
        uint8_t paddUEFIAddr[2];
        #elif defined (__linux__)
        #if defined(VMK_CROSS_COMP)
        /**
         * In VMWare we discover or send IOCTL to NVMe throught NDDK. 
         * So we will need 2 different handle for NVMe_IO and SG_IO 
         * 
         * @author 521852 (8/27/2018)
         */
        int                 fd;
        struct nvme_handle *nvmeFd;
        #else
        int                 fd;//primary handle
        #endif
        bool                scsiAddressValid;//will be true if the SCSI address is a valid address
        struct {
            uint8_t         host;//AKA SCSI adapter #
            uint8_t         channel;//AKA bus
            uint8_t         target;//AKA id number
            uint8_t         lun;//logical unit number
        }scsiAddress;
        bool                secondHandleValid;//must be true for remaining fields to be used.
        char                secondName[30];
        char                secondFriendlyName[30];
        bool                secondHandleOpened;
        #if defined(VMK_CROSS_COMP)
        /**
         * In VMWare we discover or send IOCTL to NVMe throught NDDK. 
         * So we will need 2 different handle for NVMe_IO and SG_IO 
         * 
         * @author 521852 (8/27/2018)
         */
        int                 fd2;
        struct nvme_handle *nvmeFd2;
        #else
        int                 fd2;//secondary handle. Ex: fd = sg handle opened, fd2 = sd handle opened.
        #endif
        struct {
            bool            driverVersionValid;
            uint8_t         majorVersion;
            uint8_t         minorVersion;
            uint8_t         revision;
        }sgDriverVersion;
        #if defined(VMK_CROSS_COMP)
        uint8_t paddSG[35];//TODO: need to change this based on size of NVMe handle for VMWare.
        #else
        uint8_t paddSG[35];
        #endif
        #elif defined (_WIN32)
        HANDLE              fd;
        SCSI_ADDRESS        scsi_addr;
        int                 os_drive_number;
        int                 srbtype; //this will be used to filter when a controller supports the new SCSI PassThrough EX IOCTLs
        int                 alignmentMask;//save the alignment mask. This may be needed on some controllers....not currently used but SHOULD be added later for the SCSI IOCTL DIRECT EX
        eWindowsIOCTLType   ioType;//This will be set during get_Device so we know how to talk to the drive (Mostly for ATA). Only change this if you know what you're doing.
        eWindowsIOCTLMethod ioMethod;//Use this to force using DIRECT or Double Buffered IOCTLs for each command. By default the library will decide...typically 16KiB or less will use double buffered for compatibility purposes. This is ignored for IDE and SMART IOCTLs since they are only double buffered.
        struct {
            bool smartIOSupported;//if this is false, nothing below this is valid. This just tracks whether the SMART IO is available or not. it will only be set when other ATA Pass-through methods fail. - TJE
            bool ataIDsupported;//EC command can be sent through this IO
            bool atapiIDsupported;//A1 command can be sent through this IO
            bool smartSupported;//B0 command can be sent through this IO
            uint8_t deviceBitmap;//This specifies which channel the drive is on (PATA)...might need this for sending this IO on some legacy systems. See bIDEDeviceMap here https://msdn.microsoft.com/en-us/library/windows/hardware/ff554977(v=vs.85).aspx
        }winSMARTCmdSupport;
        struct {
            bool fwdlIOSupported;
            bool allowFlexibleUseOfAPI;//Set this to true to allow using the Win10 API for FWDL for any compatible download commands. If this is false, the Win10 API will only be used on IDE_INTERFACE for an ATA download command and SCSI interface for a supported Write buffer command. If true, it will be used regardless of which command the caller is using. This is useful for pure FW updates versus testing a specific condition.
            uint32_t payloadAlignment; //From MSDN: The alignment of the image payload, in number of bytes. The maximum is PAGE_SIZE. The transfer size is a mutliple of this size. Some protocols require at least sector size. When this value is set to 0, this means that this value is invalid.
            uint32_t maxXferSize; //From MSDN: The image payload maximum size, this is used for a single command
            bool isLastSegmentOfDownload;//This should be set only when we are issuing a download command...We should find a better place for this.
            bool isFirstSegmentOfDownload;//This should be set only when we are issuing a download command...We should find a better place for this.
            //TODO: expand this struct if we need other data when we check for firmware download support on a device.
        }fwdlIOsupport;
        uint32_t adapterMaxTransferSize;//Bytes. Returned by querying for adapter properties. Can be used to know when trying to request more than the adapter or driver supports.
        //TODO: Store the device path! This may occasionally be useful to have. Longest one will probably be no more that MAX_DEVICE_ID_LEN characters. (This is defined as 200)
        //padding to keep same size as other OSs. This is to keep things similar across OSs.
        //Variable sizes based on 32 vs 64bit since handle is a void*
        #if defined (_WIN64)
            uint8_t paddWin[57];
        #else
            uint8_t paddWin[61];
        #endif //Win64 for padding
        #else
        int                 fd;//some other nix system that only needs a integer file handle
        uint8_t otherPadd[110];
        #endif
        bool                osReadWriteRecommended;//This will be set to true when it is recommended that OS read/write calls are used instead of IO read/write (typically when using SMART or IDE IOCTLs in Windows since they may not work right for read/write)
        unsigned int        last_error; // errno in Linux or GetLastError in Windows.
        struct {
            bool fileSystemInfoValid;//This must be set to true for the other bools to have any meaning. This is here because some OS's may not have support for detecting this information
            bool hasFileSystem;//This will only be true for filesystems the current OS can detect. Ex: Windows will only set this for mounted volumes it understands (NTFS, FAT32, etc). Linux may set this for more filesystem types since it can handle more than Windows by default
            bool isSystemDisk;//This will be set if the drive has a file system and the OS is running off of it. Ex: Windows' C:\Windows\System32, Linux's / & /boot, etc
        }fileSystemInfo;
        uint8_t paddEnd[4];//padd to 400 byte on UEFI. TODO: Make all OS's keep this structure the same size!!!
    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    }OSDriveInfo;
    #pragma pack(pop)
    #else
    }__attribute__((packed,aligned(1))) OSDriveInfo;
    #endif

    typedef enum _eDiscoveryOptions
    {
        DEFAULT_DISCOVERY,
        FAST_SCAN, //Gets the basic information for a quick scan like SeaChest displays on the command line.
        DO_NOT_WAKE_DRIVE, //e.g OK to send commands that do NOT access media
        NO_DRIVE_CMD,
        OPEN_HANDLE_ONLY,
        BUS_RESCAN_ALLOWED = BIT15,//this may wake the drive!
        //Flags below are bitfields...so multiple can be set. Flags above should be checked by only checking the first word of this enum.
        FORCE_ATA_PIO_ONLY = BIT16, //troubleshooting option to only send PIO versions of commands (used in get_Device/fill_Drive_Info).
        FORCE_ATA_DMA_SAT_MODE = BIT17, //troubleshooting option to send all DMA commands with protocol set to DMA in SAT CDBs
        FORCE_ATA_UDMA_SAT_MODE = BIT18, //troubleshooting option to send all DMA commands with protocol set to DMA in SAT CDBs
        GET_DEVICE_FUNCS_IGNORE_CSMI = BIT19, //use this bit in get_Device_Count and get_Device_List to ignore CSMI devices.
#if defined (ENABLE_CSMI)
        CSMI_FLAG_IGNORE_PORT = BIT25,
        CSMI_FLAG_USE_PORT = BIT26,
        CSMI_FLAG_FORCE_PRE_SAT_VU_PASSTHROUGH = BIT27, //This is for DEBUG. This uses a pre-sat passthrough CDB which may not work properly...
        CSMI_FLAG_FORCE_SSP = BIT28,
        CSMI_FLAG_FORCE_STP = BIT29,
        CSMI_FLAG_VERBOSE = BIT30,
#endif
    } eDiscoveryOptions;

    typedef int (*issue_io_func)( void * );

    #define DEVICE_BLOCK_VERSION    (5)

    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    #pragma pack(push, 1)
    #endif
    // verification for compatibility checking
    typedef struct _versionBlock
    {
        uint32_t size;      // size of enclosing structure
        uint32_t version;   // version of enclosing structure
    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    }versionBlock;
    #pragma pack(pop)
    #else
    }__attribute__((packed,aligned(1))) versionBlock;
    #endif

    // \struct typedef struct _tDevice
    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    #pragma pack(push, 1)
    #endif
    typedef struct _tDevice
    {
        versionBlock        sanity;
        OSDriveInfo         os_info;
        driveInfo           drive_info;
        void                *raid_device;
        issue_io_func       issue_io;
        eDiscoveryOptions   dFlags;
        eVerbosityLevels    deviceVerbosity;
    #if !defined (__GNUC__) || defined (__MINGW32__) || defined (__MINGW64__)
    }tDevice;
    #pragma pack(pop)
    #else
    }__attribute__((packed,aligned(1))) tDevice;
    #endif

     //Common enum for getting/setting power states.
     //This enum encompasses Mode Sense/Select commands for SCSI, Set Features for ATA
     //And Get/Set Features for NVMe. Lower layers must translated bits according to interface.
    typedef enum _eFeatureModeSelect
    {
        CURRENT_VALUE,
        CHANGEABLE_VALUE,
        DEFAULT_VALUE,
        SAVED_VALUE,
        CAPABILITIES
    } eFeatureModeSelect;

    //this enum is used to know which power conditions we are interested in...it is here so that both ATA and SCSI can see it
    typedef enum _ePowerConditionID
    {
        PWR_CND_NOT_SET = -1,
        PWR_CND_STANDBY_Z = 0x00, //value according to ATA spec.
        PWR_CND_STANDBY_Y = 0x01, //value according to ATA spec.
        PWR_CND_IDLE_A    = 0x81, //value according to ATA spec.
        PWR_CND_IDLE_B    = 0x82, //value according to ATA spec.
        PWR_CND_IDLE_C    = 0x83, //value according to ATA spec.
        PWR_CND_ACTIVE    = 0x84, //value is just for continuation (not ATA spec SCSI has 0)
        PWR_CND_ALL       = 0xFF,
        PWR_CND_RESERVED
    } ePowerConditionID;

    #define LEGACY_DRIVE_SEC_SIZE         UINT16_C(512)
    #define COMMON_4K_SIZE                UINT16_C(4096)
    #define MAX_28_BIT_LBA                UINT32_C(0xFFFFFFF)
    #define MAX_48_BIT_LBA                UINT64_C(0xFFFFFFFFFFFF)

    //the following link can be used to look-up and add additional OUIs https://standards.ieee.org/develop/regauth/oui/public.html
    typedef enum _eIEEE_OUIs
    {
        IEEE_UNKNOWN                = 0,
        IEEE_MAXTOR                 = 0x0010B9,
        IEEE_SEAGATE_SAMSUNG_HDD    = 0x0004CF,//appears to be what Seagate Samsung drives have for OUI
        IEEE_SEAGATE_NVME           = 0x0004CF,
        IEEE_SEAGATE1               = 0x000C50,
        IEEE_SEAGATE2               = 0x0011C6,
        IEEE_SEAGATE3               = 0x0014C3,
        IEEE_SEAGATE4               = 0x001862,
        IEEE_SEAGATE5               = 0x001D38,
        IEEE_SEAGATE6               = 0x002037,
        IEEE_SEAGATE7               = 0x0024B6,
        IEEE_SEAGATE8               = 0xB45253,
        IEEE_SAMSUNG_SSD            = 0x002538,
        IEEE_SAMSUNG_HDD1           = 0x0000F0,
        IEEE_SAMSUNG_HDD2           = 0x0024E9,
        IEEE_VENDOR_A                 = 0x00242F,
        IEEE_VENDOR_A_TECHNOLOGY      = 0x00A075,
    }eIEEE_OUIs;

    //http://www.linux-usb.org/usb.ids
    typedef enum _eUSBVendorIDs
    {
        USB_Vendor_Unknown                              = 0,
        USB_Vendor_Adaptec                              = 0x03F3,
        USB_Vendor_Buffalo                              = 0x0411,
        USB_Vendor_Seagate                              = 0x0477,
        USB_Vendor_Integrated_Techonology_Express_Inc   = 0x048D,
        USB_Vendor_Samsung                              = 0x04E8,
        USB_Vendor_Sunplus                              = 0x04FC,
        USB_Vendor_Alcor_Micro_Corp                     = 0x058F,
        USB_Vendor_LaCie                                = 0x059F,
        USB_Vendor_GenesysLogic                         = 0x05E3,
        USB_Vendor_Prolific                             = 0x067B,
        USB_Vendor_SanDisk_Corp                         = 0x0781,
        USB_Vendor_Silicon_Motion                       = 0x090C,
        USB_Vendor_Oxford                               = 0x0928,
        USB_Vendor_Seagate_RSS                          = 0x0BC2,
        USB_Vendor_Maxtor                               = 0x0D49,
        USB_Vendor_Phison                               = 0x0D7D,
        USB_Vendor_Kingston                             = 0x13FE, //Some online databases show patriot memory, and one also shows Phison. Most recognize this as Kingston.
        USB_Vendor_JMicron                              = 0x152D,
        USB_Vendor_ASMedia                              = 0x174C,
        USB_Vendor_SeagateBranded                       = 0x1A2A,
        USB_Vendor_ChipsBank                            = 0x1E3D,
        USB_Vendor_Dell                                 = 0x413C,
        // Add new enumerations above this line!
        USB_Vendor_MaxValue                             = 0xFFFF
    } eUSBVendorIDs;

    typedef enum _eSeagateFamily
    {
        NON_SEAGATE = 0,
        SEAGATE = BIT1,
        MAXTOR = BIT2,
        SAMSUNG = BIT3,
        LACIE = BIT4,
        SEAGATE_VENDOR_A = BIT5,
        SEAGATE_VENDOR_B = BIT6,
        SEAGATE_VENDOR_C = BIT7,
        SEAGATE_VENDOR_D = BIT8,
        SEAGATE_VENDOR_E = BIT9,
        //Ancient history
        SEAGATE_QUANTUM = BIT10, //Quantum Corp. Vendor ID QUANTUM (SCSI)
        SEAGATE_CDC = BIT11, //Control Data Systems. Vendor ID CDC (SCSI)
        SEAGATE_CONNER = BIT12, //Conner Peripherals. Vendor ID CONNER (SCSI)
        SEAGATE_MINISCRIBE = BIT13, //MiniScribe. Vendor ID MINSCRIB (SCSI)
        SEAGATE_DEC = BIT14, //Digital Equipment Corporation. Vendor ID DEC (SCSI)
        SEAGATE_PRARIETEK = BIT15, //PrarieTek. Vendor ID PRAIRIE (SCSI).
        SEAGATE_PLUS_DEVELOPMENT = BIT16, //Plus Development. Unknown detection
        SEAGATE_CODATA = BIT17, //CoData. Unknown detection
        //Recently Added
        SEAGATE_VENDOR_F = BIT18,
        SEAGATE_VENDOR_G = BIT19,
        SEAGATE_VENDOR_H = BIT20
    }eSeagateFamily;

    //The scan flags should each be a bit in a 32bit unsigned integer.
    // bits 0:7 Will be used for drive type selection.
    // bits 8:15 will be used for interface selection. So this is slightly different because if you say SCSI interface you can get back both ATA and SCSI drives if they are connected to say a SAS card
    // Linux - bit 16 will be used to change the handle that shows up from the scan.
    // Linux - bit 17 will be used to show the SD to SG mapping in linux.
    // Windows - bit 16 will be used to show the long device handle name
    // RAID interfaces (including csmi) may use bits 31:26 (so far those are the only ones used by CSMI)

    #define DEFAULT_SCAN 0
    #define ALL_DRIVES 0xFF
    #define ATA_DRIVES BIT0
    #define USB_DRIVES BIT1
    #define SCSI_DRIVES BIT2
    #define NVME_DRIVES BIT3
    #define RAID_DRIVES BIT4
    #define ALL_INTERFACES 0xFF00
    #define IDE_INTERFACE_DRIVES BIT8
    #define SCSI_INTERFACE_DRIVES BIT9
    #define USB_INTERFACE_DRIVES BIT10
    #define NVME_INTERFACE_DRIVES BIT11
    #define RAID_INTERFACE_DRIVES BIT12
    #define SD_HANDLES BIT16 //this is a Linux specific flag to show SDX handles instead of SGX handles
    #define SG_TO_SD BIT17
    #define SAT_12_BYTE BIT18
    #define SCAN_SEAGATE_ONLY BIT19
    #define AGRESSIVE_SCAN BIT20 //this can wake a drive up because a bus rescan may be issued. (currently only implemented in Windows)
#if defined (ENABLE_CSMI)
    #define ALLOW_DUPLICATE_DEVICE BIT24 //This is ONLY used by the scan_And_Print_Devs function to filter what is output from it. This does NOT affect get_Device_List.
    #define IGNORE_CSMI BIT25 //only works in Windows since Linux never adopted CSMI support. Set this to ignore CSMI devices, or compile opensea-transport without the ENABLE_CSMI preprocessor definition.
#endif

    typedef enum _eZoneReportingOptions
    {
        ZONE_REPORT_LIST_ALL_ZONES                              = 0x00,
        ZONE_REPORT_LIST_EMPTY_ZONES                            = 0x01,
        ZONE_REPORT_LIST_IMPLICIT_OPEN_ZONES                    = 0x02,
        ZONE_REPORT_LIST_EXPLICIT_OPEN_ZONES                    = 0x03,
        ZONE_REPORT_LIST_CLOSED_ZONES                           = 0x04,
        ZONE_REPORT_LIST_FULL_ZONES                             = 0x05,
        ZONE_REPORT_LIST_READ_ONLY_ZONES                        = 0x06,
        ZONE_REPORT_LIST_OFFLINE_ZONES                          = 0x07,
        ZONE_REPORT_LIST_ZONES_WITH_RESET_SET_TO_ONE            = 0x10,
        ZONE_REPORT_LIST_ZONES_WITH_NON_SEQ_SET_TO_ONE          = 0x11,
        ZONE_REPORT_LIST_ALL_ZONES_THAT_ARE_NOT_WRITE_POINTERS  = 0x3F
    }eZoneReportingOptions;

    typedef enum _eZMAction
    {
        ZM_ACTION_REPORT_ZONES          = 0x00,//dma in-in
        ZM_ACTION_CLOSE_ZONE            = 0x01,//non data-out
        ZM_ACTION_FINISH_ZONE           = 0x02,//non data-out
        ZM_ACTION_OPEN_ZONE             = 0x03,//non data-out
        ZM_ACTION_RESET_WRITE_POINTERS  = 0x04,//non data-out
    }eZMAction;

    //-----------------------------------------------------------------------------
    //
    //  get_Opensea_Transport_Version()
    //
    //! \brief   Description:  Get the API version. Alternative way is to 
    //                          read OPENSEA_TRANSPORT_VERSION from version.h
    //
    //  Entry:
    //!   \param[out] ver = apiVersionInfo version block to be filled. 
        //!
    //  Exit:
    //!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int get_Opensea_Transport_Version(apiVersionInfo *ver);

    //-----------------------------------------------------------------------------
    //
    //  get_Version_Block()
    //
    //! \brief   Description:  Get the library device block version. 
    //
    //  Entry:
    //!   \param[out] ver = versionBlock structure to be filled. 
    //!
    //  Exit:
    //!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int get_Version_Block(versionBlock * ver);

    //-----------------------------------------------------------------------------
    //
    //  get_Device()
    //
    //! \brief   Description:  Given a device name (e.g.\\PhysicalDrive0) returns the file descriptor.
    //!                        Function opens the device if everything goes well,
    //!                        it returns a file handle
    //
    //  Entry:
    //!   \param[in] filename = name of the device to open
    //!   \param[in] device = device struct to hold the handle among other information
    //!
    //  Exit:
    //!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int get_Device(const char *filename, tDevice *device);

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
    //!   \param[in] flags = bit field based mask to let application control. 
    //!                      NOTE: only csmi flags are used right now.
    //!
    //  Exit:
    //!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int get_Device_Count(uint32_t * numberOfDevices, uint64_t flags);

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
    //!   \param[in]  ver = versionBlock structure filled in by application for 
    //!                              sanity check by library. 
    //!   \param[in] flags = bitfield based mask to let application control. 
    //!                      NOTE: only csmi flags are used right now
    //!
    //  Exit:
    //!   \return SUCCESS - pass, WARN_NOT_ALL_DEVICES_ENUMERATED - some deviec had trouble being enumerated. Validate that it's drive_type is not UNKNOWN_DRIVE, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int get_Device_List(tDevice * const ptrToDeviceList, uint32_t sizeInBytes, versionBlock ver, uint64_t flags);


    //-----------------------------------------------------------------------------
    //
    //  close_Device()
    //
    //! \brief   Description:  Given a tDevice, close it's handle. 
    //
    //  Entry:
    //!   \param[in] device = device struct that holds device information. 
    //!
    //  Exit:
    //!   \return SUCCESS - pass, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int close_Device(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  scan_And_Print_Devs()
    //
    //! \brief   Description:  Scan for devices on the supported interfaces & print info.
    //!                        Function that scans for valid devices on all interfaces.
    //                         IMPORTANT: Try best to not send a command to the device.
    //
    //  Entry:
    //!   \param[in] flags = Flags for future use to control the scan
    //!   \param[in] outputInfo = pointer to an outputInfo struct to control how to output the scan information. If this is NULL, standard screen output is assumed
    //!   \param[in] scanVerbosity = the verbosity to run the scan at
    //!
    //  Exit:
    //!   \return VOID
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API void scan_And_Print_Devs(unsigned int flags, OutputInfo *outputInfo, eVerbosityLevels scanVerbosity);

    //-----------------------------------------------------------------------------
    //
    //  load_Bin_Buf()
    //
    //! \brief   Description:  load contents of binary file to buffer
    //
    //  Entry:
    //!   \param[in] filename = name of file to be read from
    //!   \param[in] myBuf = the buffer to be filled
    //!   \param[in] bufSize = amount of data to be written
    //!
    //  Exit:
    //!   \return SUCCESS on successful completion, !SUCCESS if problems encountered
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int load_Bin_Buf(char *filename, void *myBuf, size_t bufSize);

    //-----------------------------------------------------------------------------
    //
    //  scan_Drive_Type_Filter()
    //
    //! \brief   Description:  This is a filter function used by scan to determine which drives found we want to show
    //
    //  Entry:
    //!   \param[in] device - pointer to the device structure that has already been filled in by get_Device()
    //!   \param[in] scanFlags = flags used to control scan. All flags that don't pertain to drive type are ignored.
    //!
    //  Exit:
    //!   \return true = show drive, false = don't show drive
    //
    //-----------------------------------------------------------------------------
    bool scan_Drive_Type_Filter(tDevice *device, uint32_t scanFlags);

    //-----------------------------------------------------------------------------
    //
    //  scan_Drive_Type_Filter()
    //
    //! \brief   Description:  This is a filter function used by scan to determine which drive interfaces found we want to show
    //
    //  Entry:
    //!   \param[in] device - pointer to the device structure that has already been filled in by get_Device()
    //!   \param[in] scanFlags = flags used to control scan. All flags that don't pertain to drive interface are ignored.
    //!
    //  Exit:
    //!   \return true = show drive, false = don't show drive
    //
    //-----------------------------------------------------------------------------
    bool scan_Interface_Type_Filter(tDevice *device, uint32_t scanFlags);

    //-----------------------------------------------------------------------------
    //
    //  is_Seagate_Family( tDevice * device )
    //
    //! \brief   Checks if the device is a Seagate drive, a Samsung HDD drive, a Maxtor Drive, or a LaCie Drive. This should be used for features we want to allow only on Seagate Supported products (erase, etc).
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!
    //  Exit:
    //!   \return eSeagateFamily enum value. See enum for meanings
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API eSeagateFamily is_Seagate_Family(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  is_Seagate_MN( tDevice * device )
    //
    //! \brief   Checks if the passed in model number string starts with ST which means it's a seagate MN
    //
    //  Entry:
    //!   \param[in]  string - the  model number string to check
    //!
    //  Exit:
    //!   \return 1 = It is a Seagate Drive, 0 - Not a Seagate Drive
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API bool is_Seagate_MN(char* string);

    //-----------------------------------------------------------------------------
    //
    //  is_Seagate_VendorID( tDevice * device )
    //
    //! \brief   Checks if the vendor ID for a device is Seagate or SEAGATE. Useful for determinging a SAS or USB drive is a seagate drive
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!
    //  Exit:
    //!   \return 1 = It is a Seagate Drive, 0 - Not a Seagate Drive
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API bool is_Seagate_VendorID(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  is_Seagate( tDevice * device )
    //
    //! \brief   Checks if the device is a Seagate drive
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!   \param[in]  USBchildDrive - set to true to check USB child drive information. if set to false, this will automatically also check the child drive info (this is really just used for recursion in the function)
    //!
    //  Exit:
    //!   \return 1 = It is a Seagate Drive, 0 - Not a Seagate Drive
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API bool is_Seagate(tDevice *device, bool USBchildDrive);

    //-----------------------------------------------------------------------------
    //
    //  is_LaCie( tDevice * device )
    //
    //! \brief   Checks if the device is a LaCie drive
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!
    //  Exit:
    //!   \return 1 = It is a LaCie Drive, 0 - Not a LaCie Drive
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API bool is_LaCie(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  is_Samsung_String( tDevice * device )
    //
    //! \brief   Checks if the passed in string (model number or vendor id) are SAMSUNG
    //
    //  Entry:
    //!   \param[in]  string - string to check for Samsung
    //!
    //  Exit:
    //!   \return 1 = It is a Samsung Drive, 0 - Not a Samsung Drive
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API bool is_Samsung_String(char* string);

    //-----------------------------------------------------------------------------
    //
    //  is_Samsung_HDD( tDevice * device )
    //
    //! \brief   Checks if the device is a Samsung HDD drive
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!   \param[in]  USBchildDrive - set to true to check USB child drive information. if set to false, this will automatically also check the child drive info (this is really just used for recursion in the function)
    //!
    //  Exit:
    //!   \return 1 = It is a Samsung Drive, 0 - Not a Samsung Drive
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API bool is_Samsung_HDD(tDevice *device, bool USBchildDrive);

    //-----------------------------------------------------------------------------
    //
    //  is_Maxtor_String( tDevice * device )
    //
    //! \brief   Checks if the passed in string (model number or vendor id) are MAXTOR
    //
    //  Entry:
    //!   \param[in]  string - string to check if it is a maxtor name
    //!
    //  Exit:
    //!   \return 1 = It is a Maxtor Drive, 0 - Not a Maxtor Drive
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API bool is_Maxtor_String(char* string);

    //-----------------------------------------------------------------------------
    //
    //  is_Maxtor( tDevice * device )
    //
    //! \brief   Checks if the device is a Maxtor drive
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!   \param[in]  USBchildDrive - set to true to check USB child drive information. if set to false, this will automatically also check the child drive info (this is really just used for recursion in the function)
    //!
    //  Exit:
    //!   \return 1 = It is a Maxtor Drive, 0 - Not a Maxtor Drive
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API bool is_Maxtor(tDevice *device, bool USBchildDrive);

    //-----------------------------------------------------------------------------
    //
    //  is_Seagate_Model_Vendor_A( tDevice * device )
    //
    //! \brief   Checks if the device is a Seagate partnership product
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!
    //  Exit:
    //!   \return 1 = It is a Seagate Partner Drive, 0 - Not a Seagate-Partner Drive
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API bool is_Seagate_Model_Vendor_A(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  is_Vendor_A( tDevice * device )
    //
    //! \brief   Checks if the device is a Partner drive
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!   \param[in]  USBchildDrive - set to true to check USB child drive information. if set to false, this will automatically also check the child drive info (this is really just used for recursion in the function)
    //!
    //  Exit:
    //!   \return 1 = It is a Partner Drive, 0 - Not a Partner Drive
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API bool is_Vendor_A(tDevice *device, bool USBchildDrive);

    //-----------------------------------------------------------------------------
    //
    //  is_SSD( tDevice * device )
    //
    //! \brief   Checks if the device is an SSD or not. This just looks at the media type to see if it is MEDIA_SSD or MEDIA_NVM
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!
    //  Exit:
    //!   \return 1 = It is a SSD Drive, 0 - Not a SSD Drive
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API bool is_SSD(tDevice *device);

    OPENSEA_TRANSPORT_API bool is_SATA(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  is_Sector_Size_Emulation_Active( tDevice * device )
    //
    //! \brief   Checks if sector size emulation is active. This is only really useful on SAT interfaces/drivers. This will always be false when the isValid bool is set to false in thebridge info struct
    //!          This will likely only be true for some USB devices greater than 2.2TB, but SAT spec allows SAT devices to emulate different sector sizes (although any emulation is vendor specific to the SATL)
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!
    //  Exit:
    //!   \return true = sector size emulation is active, false = sector size emulation is inactive, i.e. SATL reported block size = device reported block size
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API bool is_Sector_Size_Emulation_Active(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  calculate_Checksum( uint8_t buf,  ) (OBSOLETE)
    //
    //! \brief  Calculates the ATA Spec. version of the checksum & returns the data
    //!         NOTE: 511th byte of the buffer will be changed.
    //!         This function has been replaced with a couple others in ata_helper_func.h since this is specific to ATA.
    //!
    //! A.14.7 Checksum
    //! The data structure checksum is the two?s complement of the sum of the first 511 bytes in the data structure. Each
    //! byte shall be added with eight-bit unsigned arithmetic and overflow shall be ignored. The sum of all 512 bytes of
    //! the data structure shall be zero.
    //
    //  Entry:
    //!   \param[in, out] pBuf = uint8_t buffer to perform checksum on
    //!   \param[in] blockSize = uint32_t block size
    //  Exit:
    //!   \return int SUCCESS if passes !SUCCESS if fails for some reason.
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API int calculate_Checksum(uint8_t *pBuf, uint32_t blockSize);

    //-----------------------------------------------------------------------------
    //
    //  get_Sector_Count_For_Read_Write(tDevice *device)
    //
    //! \brief  Gets the sectorCount based on the device interface. The value set is one that is most compatible across controllers/bridges and OSs
    //!         Will set 64K transfers for internal interfaces (SATA, SAS) and 32K for external (USB, IEEE1394)
    //
    //  Entry:
    //!   \param[in] device = pointer to the device struct.
    //
    //  Exit:
    //!   \return uint32_t value to use for a sector count
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API uint32_t get_Sector_Count_For_Read_Write(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  get_Sector_Count_For_512B_Based_XFers(tDevice *device)
    //
    //! \brief  Gets the sectorCount based on the device interface for commands that are based on 512B transfer blocks. 
    //!         The value set is one that is most compatible across controllers/bridges and OSs
    //!         Will set 64K transfers for internal interfaces (SATA, SAS) and 32K for external (USB, IEEE1394)
    //
    //  Entry:
    //!   \param[in] device = pointer to the device struct.
    //
    //  Exit:
    //!   \return uint32_t value to use for a sector count
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API uint32_t get_Sector_Count_For_512B_Based_XFers(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  get_Sector_Count_For_4096B_Based_XFers(tDevice *device)
    //
    //! \brief  Gets the sectorCount based on the device interface for commands that are based on 4096B (4K) transfer blocks. 
    //!         The value set is one that is most compatible across controllers/bridges and OSs
    //!         Will set 64K transfers for internal interfaces (SATA, SAS) and 32K for external (USB, IEEE1394)
    //
    //  Entry:
    //!   \param[in] device = pointer to the device struct.
    //
    //  Exit:
    //!   \return uint32_t value to use for a sector count
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API uint32_t get_Sector_Count_For_4096B_Based_XFers(tDevice *device);

    //-----------------------------------------------------------------------------
    //
    //  align_LBA()
    //
    //! \brief   Description:  This function takes an LBA, then aligns it to the beginning of a physical block. The return value is the LBA at the beginning of the physical block.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] LBA = the LBA that will be checked for alignment
    //!
    //  Exit:
    //!   \return The aligned LBA
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API uint64_t align_LBA(tDevice *device, uint64_t LBA);

    OPENSEA_TRANSPORT_API void print_Command_Time(uint64_t timeInNanoSeconds);

    OPENSEA_TRANSPORT_API void print_Time(uint64_t timeInNanoSeconds);

    OPENSEA_TRANSPORT_API void write_JSON_To_File(void *customData, char *message); //callback function

    typedef struct _removeDuplicateDriveType
    {
        uint8_t csmi;
        uint8_t raid;
    }removeDuplicateDriveType;

    OPENSEA_TRANSPORT_API int remove_Duplicate_Devices(tDevice *deviceList, volatile uint32_t * numberOfDevices, removeDuplicateDriveType rmvDevFlag);

    OPENSEA_TRANSPORT_API int remove_Device(tDevice *deviceList, uint32_t driveToRemoveIdx, volatile uint32_t * numberOfDevices);

    OPENSEA_TRANSPORT_API bool is_CSMI_Device(tDevice *device);
    OPENSEA_TRANSPORT_API bool is_Removable_Media(tDevice *device);

    bool setup_Passthrough_Hacks_By_ID(tDevice *device);

    #if defined (_DEBUG)
    //This function is more for debugging than anything else!
    void print_tDevice_Size();
    #endif//_DEBUG

#if defined (__cplusplus)
} //extern "C"
#endif
