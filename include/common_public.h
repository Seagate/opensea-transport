// SPDX-License-Identifier: MPL-2.0

//! \file common_public.h
//! \brief Defines the structures and and constants that are common to OS and Non-OS specific code.
//! Contains tDevice structure used to send commands to any supported device in the system.
//! \copyright
//! Do NOT modify or remove this copyright and license
//!
//! Copyright (c) 2012-2024 Seagate Technology LLC and/or its Affiliates, All Rights Reserved
//!
//! This software is subject to the terms of the Mozilla Public License, v. 2.0.
//! If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "bit_manip.h"
#include "code_attributes.h"
#include "common_types.h"
#include "memory_safety.h"
#include "type_conversion.h"

#include "version.h"
#if defined(VMK_CROSS_COMP)
#    include "vm_nvme_lib.h"
#endif
#if defined(UEFI_C_SOURCE)
#    include <Protocol/DevicePath.h>      //for device path union/structures
#    include <Protocol/ScsiPassThruExt.h> //for TARGET_MAX_BYTES definition
#endif

#if defined(__FreeBSD__)
// Not including this here even though I thought I might need to because it causes compilation errors all over...not
// really sure why, but this worked...-TJE #include <camlib.h> //for cam structure held in tDevice
#endif

#if defined(_WIN32) && !defined(_NTDDSCSIH_)
#    include <ntddscsi.h>
#endif //_WIN32 & !_NTDDSCSIH_

#include "ciss_helper.h" //because this holds a structure to help with issuing CCISS commands
#include "csmi_helper.h" //because the device structure holds some csmi support structure for when we can issue csmi passthrough commands.

#if defined(__cplusplus)
#    define __STDC_FORMAT_MACROS // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
extern "C"
{
#endif

// This is a bunch of stuff for creating opensea-transport as a dynamic library (DLL in Windows or shared object in
// linux)
#if defined(OPENSEA_TRANSPORT_API)
#    undef(OPENSEA_TRANSPORT_API)
#endif

#if defined(EXPORT_OPENSEA_TRANSPORT) && defined(STATIC_OPENSEA_TRANSPORT)
#    error "The preprocessor definitions EXPORT_OPENSEA_TRANSPORT and STATIC_OPENSEA_TRANSPORT cannot be combined!"
#elif defined(EXPORT_OPENSEA_TRANSPORT)
#    if defined(_DEBUG) && !defined(OPENSEA_TRANSPORT_COMPILATION_MESSAGE_OUTPUT)
#        pragma message("Compiling opensea-transport as exporting DLL!")
#        define OPENSEA_TRANSPORT_COMPILATION_MESSAGE_OUTPUT
#    endif
#    define OPENSEA_TRANSPORT_API DLL_EXPORT
#elif defined(IMPORT_OPENSEA_TRANSPORT)
#    if defined(_DEBUG) && !defined(OPENSEA_TRANSPORT_COMPILATION_MESSAGE_OUTPUT)
#        pragma message("Compiling opensea-transport as importing DLL!")
#        define OPENSEA_TRANSPORT_COMPILATION_MESSAGE_OUTPUT
#    endif
#    define OPENSEA_TRANSPORT_API DLL_IMPORT
#else
#    if defined(_DEBUG) && !defined(OPENSEA_TRANSPORT_COMPILATION_MESSAGE_OUTPUT)
#        pragma message("Compiling opensea-transport as a static library!")
#        define OPENSEA_TRANSPORT_COMPILATION_MESSAGE_OUTPUT
#    endif
#    define OPENSEA_TRANSPORT_API
#endif

#define SEAGATE_VENDOR_ID          (0x1BB1)

#define OPENSEA_MAX_CONTROLLERS    (8U)
#define MAX_DEVICES_PER_CONTROLLER (256U)
#define MAX_DEVICES_TO_SCAN        (OPENSEA_MAX_CONTROLLERS * MAX_DEVICES_PER_CONTROLLER)
#define MAX_DRIVER_NAME            40
#define MAX_DRIVER_VER_STR         24

#define SERIAL_NUM_LEN             (20) // Going with ATA lengths
#define MODEL_NUM_LEN              (40)
#define FW_REV_LEN                 (8)
#define T10_VENDOR_ID_LEN          (8)

    typedef struct s_apiVersionInfo
    {
        uint8_t majorVersion;
        uint8_t minorVersion;
        uint8_t patchVersion;
        uint8_t reserved;
    } apiVersionInfo;

    // These need to be moved to ata_helper.h
    // NOTE: This structure is defined like this without bitfield ON PURPOSE.
    // DO NOT ADD BITFIELDS
    // DO NOT ADD > 16bit types here in order to keep this packed by compilers correctly.
    // IF this structure is used in another, make sure it begins at a MINIMUM of a 2 byte offset for correct addressing,
    // however 8 byte is recommended! Previously, this was packed and aligned with attributes/pragmas to the compiler,
    // but those were removed after properly fixing this structure so that those extra instructions aren't needed. This
    // prevents potential misaligned addresses on some CPU architectures that require special pointer alignment for data
    // access. This does not affect x86, but does for some like SPARC and possibly ARM.

#define ATA_IDENTIFY_SN_LENGTH (20)
#define ATA_IDENTIFY_MN_LENGTH (40)
#define ATA_IDENTIFY_FW_LENGTH (8)

    typedef struct s_tAtaIdentifyData
    {
        uint16_t Word000;
        uint16_t Word001;
        uint16_t Word002;
        uint16_t Word003;
        uint16_t Word004;
        uint16_t Word005;
        uint16_t Word006;
        uint16_t Word007;
        uint16_t Word008;
        uint16_t Word009;
        union
        {
            uint8_t SerNum[ATA_IDENTIFY_SN_LENGTH];
            struct
            {
                uint16_t Word010;
                uint16_t Word011;
                uint16_t Word012;
                uint16_t Word013;
                uint16_t Word014;
                uint16_t Word015;
                uint16_t Word016;
                uint16_t Word017;
                uint16_t Word018;
                uint16_t Word019;
            }; // anonymous to make sure all words are easily accessed. If this creates too many warnings, we can give
               // it the name idSNwords or something-TJE
        }; // anonymous to make access to SN or SN words easier
        uint16_t Word020;
        uint16_t Word021;
        uint16_t Word022;
        union
        {
            uint8_t FirmVer[ATA_IDENTIFY_FW_LENGTH]; // 23 24 25 26
            struct
            {
                uint16_t Word023;
                uint16_t Word024;
                uint16_t Word025;
                uint16_t Word026;
            }; // anonymous to make sure all words are easily accessed. If this creates too many warnings, we can give
               // it the name idFWwords or something-TJE
        }; // anonymous to make access to FW or FW words easier
        union
        {
            uint8_t ModelNum[ATA_IDENTIFY_MN_LENGTH]; // 27 ... 46
            struct
            {
                uint16_t Word027;
                uint16_t Word028;
                uint16_t Word029;
                uint16_t Word030;
                uint16_t Word031;
                uint16_t Word032;
                uint16_t Word033;
                uint16_t Word034;
                uint16_t Word035;
                uint16_t Word036;
                uint16_t Word037;
                uint16_t Word038;
                uint16_t Word039;
                uint16_t Word040;
                uint16_t Word041;
                uint16_t Word042;
                uint16_t Word043;
                uint16_t Word044;
                uint16_t Word045;
                uint16_t Word046;
            }; // anonymous to make sure all words are easily accessed. If this creates too many warnings, we can give
               // it the name idMNwords or something-TJE
        }; // anonymous to make access to MN or MN words easier
        uint16_t Word047;
        uint16_t Word048;
        uint16_t Word049;

        uint16_t Word050;
        uint16_t Word051;
        uint16_t Word052;
        uint16_t Word053;
        uint16_t Word054;
        uint16_t Word055;
        uint16_t Word056;
        uint16_t Word057; // Total sectors CHS
        uint16_t Word058; // Total sectors CHS
        uint16_t Word059;

        uint16_t Word060; // Total LBAs (28bit)
        uint16_t Word061;
        uint16_t Word062;
        uint16_t Word063;
        uint16_t Word064;
        uint16_t Word065;
        uint16_t Word066;
        uint16_t Word067;
        uint16_t Word068;
        uint16_t Word069;

        uint16_t Word070;
        uint16_t Word071;
        uint16_t Word072;
        uint16_t Word073;
        uint16_t Word074;
        uint16_t Word075;
        uint16_t Word076;
        uint16_t Word077;
        uint16_t Word078;
        uint16_t Word079;

        uint16_t Word080;
        uint16_t Word081;
        uint16_t Word082;
        uint16_t Word083;
        uint16_t Word084;
        uint16_t Word085;
        uint16_t Word086;
        uint16_t Word087;
        uint16_t Word088;
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

        uint16_t Word100; // total addressable logical sectors
        uint16_t Word101; // total addressable logical sectors
        uint16_t Word102; // total addressable logical sectors
        uint16_t Word103; // total addressable logical sectors
        uint16_t Word104;
        uint16_t Word105;
        uint16_t Word106;
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
        uint16_t Word117; // sector size
        uint16_t Word118; // sector size
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

        uint16_t Word150;
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
    } tAtaIdentifyData, *ptAtaIdentifyData;

    M_STATIC_ASSERT(sizeof(tAtaIdentifyData) == 512, ata_identify_must_be_512_bytes);

    // All of the NVME structs in here were moved here to fix a circular include issue
    typedef struct s_nvmeIDPowerState
    {
        uint16_t maxPower; /* centiwatts */
        uint8_t  rsvd2;
        uint8_t  flags;
        uint32_t entryLat; /* microseconds */
        uint32_t exitLat;  /* microseconds */
        uint8_t  readTPut;
        uint8_t  readLat;
        uint8_t  writeLput;
        uint8_t  writeLat;
        uint16_t idlePower;
        uint8_t  idleScale;
        uint8_t  rsvd19;
        uint16_t activePower;
        uint8_t  activeWorkScale;
        uint8_t  rsvd23[9];
    } nvmeIDPowerState;

#define NVME_CTRL_IDENTIFY_SN_LEN (20)
#define NVME_CTRL_IDENTIFY_MN_LEN (40)
#define NVME_CTRL_IDENTIFY_FW_LEN (8)

    typedef struct s_nvmeIDCtrl
    {
        // controller capabilities and features
        uint16_t vid;
        uint16_t ssvid;
        char     sn[NVME_CTRL_IDENTIFY_SN_LEN];
        char     mn[NVME_CTRL_IDENTIFY_MN_LEN];
        char     fr[NVME_CTRL_IDENTIFY_FW_LEN];
        uint8_t  rab;
        uint8_t  ieee[3];
        uint8_t  cmic;
        uint8_t  mdts;
        uint16_t cntlid;
        uint32_t ver;
        uint32_t rtd3r;
        uint32_t rtd3e;
        uint32_t oaes;
        uint32_t ctratt;
        uint16_t rrls;
        uint8_t  reservedBytes110_102[9];
        uint8_t  cntrltype;
        uint8_t  fguid[16]; // 128bit identifier
        uint16_t crdt1;
        uint16_t crdt2;
        uint16_t crdt3;
        uint8_t  reservedBytes239_134[106];
        uint8_t  nvmManagement[16];
        // Admin command set attribues & optional controller capabilities
        uint16_t oacs;
        uint8_t  acl;
        uint8_t  aerl;
        uint8_t  frmw;
        uint8_t  lpa;
        uint8_t  elpe;
        uint8_t  npss;
        uint8_t  avscc;
        uint8_t  apsta;
        uint16_t wctemp;
        uint16_t cctemp;
        uint16_t mtfa;
        uint32_t hmpre;
        uint32_t hmmin;
        uint8_t  tnvmcap[16];
        uint8_t  unvmcap[16];
        uint32_t rpmbs;
        uint16_t edstt;
        uint8_t  dsto;
        uint8_t  fwug;
        uint16_t kas;
        uint16_t hctma;
        uint16_t mntmt;
        uint16_t mxtmt;
        uint32_t sanicap;
        uint32_t hmminds;
        uint16_t hmmaxd;
        uint16_t nsetidmax;
        uint16_t endgidmax;
        uint8_t  anatt;
        uint8_t  anacap;
        uint32_t anagrpmax;
        uint32_t nanagrpid;
        uint32_t pels;
        uint16_t domainIdentifier;
        uint8_t  reservedBytes367_358[10];
        uint8_t  megcap[16];
        uint8_t  reservedBytes511_384[128];
        // NVM command set attributes;
        uint8_t  sqes;
        uint8_t  cqes;
        uint16_t maxcmd;
        uint32_t nn;
        uint16_t oncs;
        uint16_t fuses;
        uint8_t  fna;
        uint8_t  vwc;
        uint16_t awun;
        uint16_t awupf;
        union
        {
            uint8_t nvscc;
            uint8_t icsvscc;
        };
        uint8_t          nwpc;
        uint16_t         acwu;
        uint16_t         optionalCopyFormatsSupported;
        uint32_t         sgls;
        uint32_t         mnan;
        uint8_t          maxdna[16];
        uint32_t         maxcna;
        uint8_t          reservedBytes767_564[204];
        char             subnqn[256];
        uint8_t          reservedBytes1791_1024[768];
        uint8_t          nvmeOverFabrics[256];
        nvmeIDPowerState psd[32];
        uint8_t          vs[1024];
    } nvmeIDCtrl;

    M_STATIC_ASSERT(sizeof(nvmeIDCtrl) == 4096, nvme_ctrl_identify_must_be_4096_bytes);

    typedef struct s_nvmeLBAF
    {
        uint16_t ms;
        uint8_t  lbaDS;
        uint8_t  rp;
    } nvmeLBAF;

    typedef struct s_nvmeIDNameSpaces
    {
        uint64_t nsze;
        uint64_t ncap;
        uint64_t nuse;
        uint8_t  nsfeat;
        uint8_t  nlbaf;
        uint8_t  flbas;
        uint8_t  mc;
        uint8_t  dpc;
        uint8_t  dps;
        uint8_t  nmic;
        uint8_t  rescap;
        uint8_t  fpi;
        uint8_t  dlfeat;
        uint16_t nawun;
        uint16_t nawupf;
        uint16_t nacwu;
        uint16_t nabsn;
        uint16_t nabo;
        uint16_t nabspf;
        uint16_t noiob;
        uint8_t  nvmcap[16]; // 128bit number
        uint16_t npwg;
        uint16_t npwa;
        uint16_t npdg;
        uint16_t npda;
        uint16_t nows;
        uint16_t mssrl;
        uint16_t mcl;
        uint8_t  msrc;
        uint8_t  rsvd40[11]; // bytes 91:81
        uint32_t anagrpid;
        uint8_t  rsvd1[3]; // bytes 98:96
        uint8_t  nsattr;
        uint16_t nvmsetid;
        uint16_t endgid;
        uint8_t  nguid[16];
        uint8_t  eui64[8];
        nvmeLBAF lbaf[64];
        uint8_t  vs[3712];
    } nvmeIDNameSpaces;

    M_STATIC_ASSERT(sizeof(nvmeIDNameSpaces) == 4096, nvme_namespace_identify_must_be_4096_bytes);

    typedef struct s_nvmeIdentifyData
    {
        nvmeIDCtrl       ctrl;
        nvmeIDNameSpaces ns; // Currently we only support 1 NS - Revisit.
    } nvmeIdentifyData;

    typedef struct s_ataReturnTFRs
    {
        uint8_t error;
        uint8_t secCntExt;
        uint8_t secCnt;
        uint8_t lbaLowExt;
        uint8_t lbaLow;
        uint8_t lbaMidExt;
        uint8_t lbaMid;
        uint8_t lbaHiExt;
        uint8_t lbaHi;
        uint8_t device;
        uint8_t status;
        uint8_t padding[5]; // empty padding to make sure this structure endds on an 8byte aligned boundary
    } ataReturnTFRs;

// Defined by SPC3 as the maximum sense length
#define SPC3_SENSE_LEN   UINT8_C(252)
#define SPC_INQ_DATA_LEN UINT8_C(96)
#define VPD_83H_LEN      64
    typedef struct s_tVpdData
    {
        uint8_t inquiryData[SPC_INQ_DATA_LEN]; // INQ_RETURN_DATA_LENGTH
        uint8_t vpdPage83[VPD_83H_LEN];
    } tVpdData;

    typedef enum eMediaTypeEnum
    {
        MEDIA_HDD       = 0, // rotating media, HDD (ata or scsi)
        MEDIA_SSD       = 1, // SSD (ata or scsi or nvme)
        MEDIA_SSM_FLASH = 2, // Solid state flash module - USB flash drive/thumb drive
        MEDIA_SSHD      = 3, // Hybrid drive.
        MEDIA_OPTICAL   = 4, // CD/DVD/etc drive media
        MEDIA_TAPE      = 5, // Tape Drive media
        MEDIA_NVM       = 6, // All NVM drives
        MEDIA_UNKNOWN        // anything else we find should get this
    } eMediaType;

    typedef enum eDriveTypeEnum
    {
        UNKNOWN_DRIVE,
        ATA_DRIVE,
        SCSI_DRIVE,
        RAID_DRIVE,
        NVME_DRIVE,
        ATAPI_DRIVE,
        FLASH_DRIVE,      // This is a USB thumb drive/flash drive or an SD card, or compact flash, etc
        LEGACY_TAPE_DRIVE // not currently used...
    } eDriveType;

    typedef enum eInterfaceTypeEnum
    {
        UNKNOWN_INTERFACE,
        IDE_INTERFACE,
        SCSI_INTERFACE,
        RAID_INTERFACE,
        NVME_INTERFACE,
        USB_INTERFACE,
        MMC_INTERFACE,
        SD_INTERFACE,
        IEEE_1394_INTERFACE,
    } eInterfaceType;

    // revisit this later as this may not be the best way we want to do this
    typedef struct s_bridgeInfo
    {
        bool     isValid;
        uint16_t childSectorAlignment; // This will usually be set to 0 on newer drives. Older drives may set this
                                       // alignment differently
        uint8_t  padd0[5];
        char     childDriveMN[MODEL_NUM_LEN + 1];
        uint8_t  padd1[7];
        char     childDriveSN[SERIAL_NUM_LEN + 1];
        uint8_t  padd2[3];
        char     childDriveFW[FW_REV_LEN + 1];
        uint8_t  padd3[7];
        uint64_t childWWN;
        char     t10SATvendorID[9]; // VPD page 89h
        uint8_t  padd4[7];
        char     SATproductID[17]; // VPD page 89h
        uint8_t  padd5[7];
        char     SATfwRev[9]; // VPD page 89h
        uint8_t  padd6[7];
        uint32_t childDeviceBlockSize;    // This is the logical block size reported by the drive
        uint32_t childDevicePhyBlockSize; // This is the physical block size reported by the drive.
        uint64_t childDeviceMaxLba;
    } bridgeInfo;

    typedef enum eAdapterInfoTypeEnum
    {
        ADAPTER_INFO_UNKNOWN, // unknown generally means it is not valid or present and was not discovred by low-level
                              // OS code
        ADAPTER_INFO_USB,
        ADAPTER_INFO_PCI,
        ADAPTER_INFO_IEEE1394, // supported under linux today
    } eAdapterInfoType;

    // this structure may or may not be populated with some low-level device adapter info. This will hold USB or
    // PCI/PCIe vendor, product, and revision codes which may help filter capabilities.
    typedef struct s_adapterInfo
    {
        bool             vendorIDValid;
        bool             productIDValid;
        bool             revisionValid;
        bool             specifierIDValid;
        eAdapterInfoType infoType;
        // USB and PCI devices use uint16's for vendor product and revision. IEEE1394 uses uint32's since most of these
        // are 24bit numbers Would an anonymous union for different types be easier??? USB  vs PCI vs IEEE1394???
        uint32_t vendorID;
        uint32_t productID;
        uint32_t revision;
        uint32_t specifierID; // Used on IEEE1394 only
    } adapterInfo;

    typedef struct s_driverInfo
    {
        char driverName[MAX_DRIVER_NAME];
        char driverVersionString[MAX_DRIVER_VER_STR]; // raw, unparsed string in case parsing into below values goes
                                                      // wrong due to variability in how this is reported between linux
                                                      // drivers.-TJE
        bool     majorVerValid;
        bool     minorVerValid;
        bool     revisionVerValid;
        bool     buildVerValid;
        uint8_t  reserved[4];
        uint32_t driverMajorVersion;
        uint32_t driverMinorVersion;
        uint32_t driverRevision;
        uint32_t driverBuildNumber; // Likely Windows only
    } driverInfo;

    typedef enum eATASynchronousDMAModeEnum
    {
        ATA_DMA_MODE_NO_DMA,
        ATA_DMA_MODE_DMA,
        ATA_DMA_MODE_MWDMA,
        ATA_DMA_MODE_UDMA
    } eATASynchronousDMAMode;

    typedef enum ePassthroughTypeEnum
    {
        ATA_PASSTHROUGH_SAT = 0, // Should be used unless you know EXACTLY the right pass through to use for a device.
        // All values below are for legacy USB support. They should only be used when you know what you are doing.
        ATA_PASSTHROUGH_CYPRESS,
        ATA_PASSTHROUGH_PROLIFIC,
        ATA_PASSTHROUGH_TI,
        ATA_PASSTHROUGH_NEC,
        ATA_PASSTHROUGH_PSP, // Some PSP drives use this passthrough and others use SAT...it's not clear if this was
                             // ever even used. If testing for it, test it last.
        ATA_PASSTHROUGH_END_LEGACY_USB =
            ATA_PASSTHROUGH_PSP, // This is set because all legacy USB passthrough's should proceed other
                                 // driver/controller passthroughs on non-USB interfaces.
        ATA_PASSTHROUGH_BEGIN_NON_USB =
            30, // set so if we need to find a passthrough that may be available for drivers or non-USB controllers,
                // they can start at this value.Assuming there will never be 30 different USB passthrough types - TJE
        ATA_PASSTHROUGH_CSMI, // This is a legacy CDB that is implemented in case old CSMI drivers are encountered that
                              // follow the original CSMI spec found online that defines this CDB instead of SAT. It is
                              // not currently tested - TJE
        ATA_PASSTHROUGH_UNKNOWN = 99, // final value to be used by ATA passthrough types
        // NVMe stuff defined here. All NVMe stuff should be 100 or higher with the exception of the default system
        // passthrough
        NVME_PASSTHROUGH_SYSTEM = 0, // This is for NVMe devices to use the system passthrough. This is the default
                                     // since this is most NVMe devices.
        NVME_PASSTHROUGH_JMICRON       = 100,
        NVME_PASSTHROUGH_ASMEDIA       = 101, // ASMedia packet command, which is capable of passing any command
        NVME_PASSTHROUGH_ASMEDIA_BASIC = 102, // ASMedia command that is capable of only select commands. Must be after
                                              // full passthrough that way when trying one passthrough after another it
                                              // can properly find full capabilities before basic capabilities.
        NVME_PASSTHROUGH_REALTEK = 103,
        // Add other vendor unique SCSI to NVMe passthrough here
        NVME_PASSTHROUGH_UNKNOWN,
        // No passthrough
        PASSTHROUGH_NONE = INT32_MAX
    } ePassthroughType;

    typedef struct s_ataOptions
    {
        eATASynchronousDMAMode dmaMode;
        bool                   dmaSupported;
        bool                   readLogWriteLogDMASupported;
        bool                   readBufferDMASupported;
        bool                   writeBufferDMASupported;
        bool                   downloadMicrocodeDMASupported;
        bool                   taggedCommandQueuingSupported;
        bool                   nativeCommandQueuingSupported;
        bool                   readWriteMultipleSupported;
        uint8_t                logicalSectorsPerDRQDataBlock;
        bool                   isParallelTransport;
        bool isDevice1;   // Don't rely on this. Only here for some OS's/passthroughs. Most shouldn't need this. SAT or
                          // the OS's passthrough will ignore this bit in the commands anyways.
        bool chsModeOnly; // AKA LBA not supported. Only really REALLY old drives should set this.
        bool writeUncorrectableExtSupported;
        bool fourtyEightBitAddressFeatureSetSupported;
        bool generalPurposeLoggingSupported;
        bool
            alwaysCheckConditionAvailableBit; // this will cause all commands to set the check condition bit. This means
                                              // any ATA Passthrough command should always get back an ATA status which
                                              // may help with sense data and judging what went wrong better. Be aware
                                              // that this may not be liked on some devices and some may just ignore it.
        bool enableLegacyPassthroughDetectionThroughTrialAndError; // This must be set to true in order to work on
                                                                   // legacy (ancient) passthrough if the VID/PID is not
                                                                   // in the list and not read from the system.
        bool senseDataReportingEnabled; // this is to track when the RTFRs may contain a sense data bit so it can be
                                        // read automatically.
        uint8_t forceSATCDBLength; // set this to 12, 16, or 32 to force a specific CDB length to use. If you set 12,
                                   // but send an extended command 16B will be used if any extended registers are set.
                                   // Same with 32B will be used if ICC or AUX are set.
        bool sataReadLogDMASameAsPIO; // not all SATA drives allow reading SATA specific pages with the DMA command.
                                      // This specifies that it is allowed. (NCQ error log, phy event counters log, etc)
        bool noNeedLegacyDeviceHeadCompatBits; // original ATA spec required bits 7 and 5 to be set to 1. This was
                                               // removed a long time ago, but can affect just about any pata device.
                                               // This helps change when to set them as they are not needed on SATA (or
                                               // shouldn't be)
        bool    dcoDMASupported;               // DCO identify and DCO set DMA commands are supported.
        bool    hpaSecurityExtDMASupported;    // HPA security extension DMA commands are supported.
        bool    sanitizeOverwriteDefinitiveEndingPattern;
        uint8_t reserved[4]; // reserved padding to keep 8 byte aligned structure for any necessary flags in the future.
    } ataOptions;

    typedef enum eZonedDeviceTypeEnum
    {
        ZONED_TYPE_NOT_ZONED      = 0,
        ZONED_TYPE_HOST_AWARE     = 1,
        ZONED_TYPE_DEVICE_MANAGED = 2,
        ZONED_TYPE_RESERVED       = 3,
        ZONED_TYPE_HOST_MANAGED   = 4
    } eZonedDeviceType;

    // This is used by the software SAT translation layer. DO NOT Update this directly
    typedef struct s_softwareSATFlags
    {
        bool identifyDeviceDataLogSupported; // TODO: each supported subpage of this log
        bool deviceStatisticsSupported; // if set to true, any 1 of the bools in the following struct is set to true
                                        // (supported)
        struct
        {
            bool rotatingMediaStatisticsPageSupported;
            bool generalErrorStatisticsSupported;
            bool temperatureStatisticsSupported;
            bool solidStateDeviceStatisticsSupported;
            bool generalStatisitcsSupported;
            bool dateAndTimeTimestampSupported; // on general statistics page
        } deviceStatsPages;
        bool currentInternalStatusLogSupported; // Needed for Error history mode of the read buffer command
        bool savedInternalStatusLogSupported;   // Needed for Error history mode of the read buffer command
        bool deferredDownloadSupported;         // Read from the identify device data log
        bool hostLogsSupported;                 // log addresses 80h - 9Fh
        bool senseDataDescriptorFormat; // DO NOT SET DIRECTLY! This should be changed through a mode select command to
                                        // the software SAT layer. false = fixed format, true = descriptor format
        bool dataSetManagementXLSupported; // Needed to help the translator know when this command is supported so it
                                           // can be used.
        bool          zeroExtSupported;
        uint8_t       rtfrIndex;
        ataReturnTFRs ataPassthroughResults[16];
        // Other flags that would simplify the software SAT code:
        //  it may be possible to combine software SAT and drive_info flags to simplify this all-TJE
        // SMART supported/enabled
        // SMART self test support
        // long self test time
        // SMART error logging
        // TRIM support (rzat, drat) + sectors per trim
        // dataset management xl support
        // write uncorrectable ext support
        // EPC supported/enabled
        // Legacy standby timer matches values in standard (versus being vendor specific)
        // APM supported/enabled and current value. Store initial value of APM when software SAT translator was started
        // for default page as well. save initial read-look ahead and write cache settings when SAT translator was
        // started for default values separate supported/enabled flags for these features as well. maxLBA logical block
        // size physical block size exponenet GPL supported detected pata vs SATA drive current transfer mode and
        // supported modes (UDMA, MWDMA, PIO, etc) store last value of legacy standby timer media SN WWN MN? SN?
        // download microcode support (dma, modes, etc)
        // Sanitize modes supported
    } softwareSATFlags;

// This is for test unit ready after failures to keep up performance on devices that slow down a LOT durring error
// processing (USB mostly)
#define TURF_LIMIT        10
#define MAX_VPD_ATTEMPTS  5
#define MAX_LP_ATTEMPTS   5
#define MAX_MP6_ATTEMPTS  8
#define MAX_MP10_ATTEMPTS 8
    // The passthroughHacks structure is to hold information to help with passthrough on OSs, USB adapters, SCSI
    // adapters, etc. Most of this is related to USB adapters though.
    typedef struct s_passthroughHacks
    {
        // generic information up top.
        bool hacksSetByReportedID; // This is true if the code was able to read and set hacks based on reported vendor
                                   // and product IDs from lower levels. If this is NOT set, then the information below
                                   // is set either by trial and error or by known product identification matches.
        bool someHacksSetByOSDiscovery; // Will be set if any of the below are set by default by the OS level code. This
                                        // may happen in Windows for ATA/SCSI passthrough to ATA devices
        ePassthroughType passthroughType; // This should be left alone unless you know for a fact which passthrough to
                                          // use. SAT is the default and should be used unless you know you need a
                                          // legacy (pre-SAT) passthrough type.
        bool testUnitReadyAfterAnyCommandFailure; // This should be done whenever we have a device that is known to
                                                  // increase time to return response to bad commands. Many USB bridges
                                                  // need this.
        uint8_t
            turfValue; // This holds the number of times longer it takes a device to respond without test unit ready.
                       // This is held here to make it easier to change library wide without retesting a device.
        // SCSI hacks are those that relate to things to handle when issuing SCSI commands that may be translated
        // improperly in some cases.
        struct
        {
            /*This comment breaks down each ATA passthrough hack based on the output short-names from
            openSeaChest_PassthroughTest UNA - unitSNAvailable RW6 - readWrite.rw6 RW10 - readWrite.rw10 RW12 -
            readWrite.rw12 RW16 - readWrite.rw16 NVPD - noVPDPages NMP - noModePages NLP - noLogPages NLPS -
            noLogSubPages MP6 - mode6bytes NMSP - noModeSubPages NRSUPOP - noReportSupportedOperations SUPSOP -
            reportSingleOpCodes REPALLOP - reportAllOpCodes SECPROT - securityProtocolSupported SECPROTI512 -
            securityProtocolWithInc512 PRESCSI2 - preSCSI2InqData (uncommon and the fields to specify offsets and
            lengths must be handled manually as the software cannot report this by itself)
            //NORWZ/NZTL - not currently handled. No zero length on read or write commands since adapter doesn't handle
            these properly. MXFER - maxTransferLength (bytes) WBND - write buffer no deferred download. PMC specific
            workaround at this time.-TJE MP6FORSPZ - use mode sense/select 6 for pages with subpage 0. Some USB adapters
            will support a page only with mode sense/select 6 but will not support the same page with the 10 byte
            command.
            TODO: More hacks for strange adapters as needed can be added in here.
            */
            bool unitSNAvailable; // This means we can request this page even if other VPD pages don't work.
            struct
            {
                bool available; // means that the bools below have been set. If not, need to use default read/write
                                // settings in the library.
                bool rw6;
                bool rw10;
                bool rw12;
                bool rw16;
            } readWrite;
            bool noVPDPages;  // no VPD pages are supported. The ONLY excetion to this is the unitSNAvailable bit above.
                              // Numerous USBs tested will only support that page...not even the list of pages will be
                              // supported by them.
            bool noModePages; // no mode pages are supported
            bool noLogPages;  // no mode pages are supported
            bool noLogSubPages;
            bool mode6bytes;                  // mode sense/select 6 byte commands only
            bool noModeSubPages;              // Subpages are not supported, don't try sending these commands
            bool noReportSupportedOperations; // report supported operation codes command is not supported.
            bool reportSingleOpCodes; // reporting supported operation codes specifying a specific operation code is
                                      // supported by the device.
            bool reportAllOpCodes;    // reporting all operation codes is supported by the device.
            bool securityProtocolSupported;  // SCSI security protocol commands are supported
            bool securityProtocolWithInc512; // SCSI security protocol commands are ONLY supported with the INC512 bit
                                             // set.
            bool preSCSI2InqData; // If this is true, then the struct below is intended to specify where, and how long,
                                  // the fields are for product ID, vendorID, revision, etc. This structure will likely
                                  // need multiple changes as these old devices are encountered and work is done to
                                  // support them - TJE
            struct
            {
                uint8_t productIDOffset; // If 0, not valid or reported
                uint8_t productIDLength; // If 0, not valid or reported
                uint8_t productRevOffset;
                uint8_t productRevLength;
                uint8_t vendorIDOffset;
                uint8_t vendorIDLength;
                uint8_t serialNumberOffset;
                uint8_t serialNumberLength;
            } scsiInq;
            bool writeBufferNoDeferredDownload; // Write buffer is filtered and does not allow updating firmware using
                                                // deferred download. Specific to PMC 8070 for now
            uint8_t mp6sp0Success; // this is for the next option so that it can be set when detected automatically-TJE
            bool    useMode6BForSubpageZero; // mode pages with subpage zero are supported, but only using 6 byte mode
                                             // commands for some unknown reason.
            uint8_t attemptedMP6s;
            uint8_t successfulMP6s; // counter for number of times mode page 6 read correctly. Can be used for automatic
                                    // setting of hacks.
            uint8_t attemptedMP10s;
            uint8_t successfulMP10s; // counter for number of times mode page 10 read correctly. Can be used for
                                     // automatic setting of hacks.
            uint8_t attemptedLPs;
            uint8_t successfulLPs; // counter for number of times a log page read correctly. Can be used for automatic
                                   // setting of hacks.
            uint8_t attemptedVPDs;
            uint8_t successfulVPDs; // counter for number of times a VPD page read correctly. Can be used for automatic
                                    // setting of hacks.
            bool cmdDTchecked;   // SPC cmdDT to check for commands supported. Old and replaced by report supported op
                                 // codes. This is here so it can be checked in a function and stored while running
            bool cmdDTSupported; // If above bool is true, then this holds if cmdDT is supported and can be used to
                                 // check if commands are supported or not. Really only for old drives -TJE
            uint8_t  reserved[1];
            uint32_t maxTransferLength; // Maximum SCSI command transfer length in bytes. Mostly here for USB where
                                        // translations aren't accurate or don't show this properly.
            bool noSATVPDPage; // when this is set, the SAT VPD is not available and should not be read, skipping ahead
                               // to instead directly trying a passthrough command
            uint8_t reserved2[3];
        } scsiHacks;
        // ATA Hacks refer to SAT translation issues or workarounds.
        struct
        {
            /*This comment breaks down each ATA passthrough hack based on the output short-names from
            openSeaChest_PassthroughTest SCTSM - smartCommandTransportWithSMARTLogCommandsOnly A1 -
            useA1SATPassthroughWheneverPossible (This hack is obsolete now as A1 is the default with an automatic
            software retry now) NA1 - a1NeverSupported A1EXT - a1ExtCommandWhenPossible RS - returnResponseInfoSupported
            RSTD - returnResponseInfoNeedsTDIR
            RSIE - returnResponseIgnoreExtendBit
            TSPIU - alwaysUseTPSIUForSATPassthrough
            CHK - alwaysCheckConditionAvailable
            FDMA - alwaysUseDMAInsteadOfUDMA
            NDMA - dmaNotSupported
            PARTRTFR - partialRTFRs
            NORTFR - noRTFRsPossible
            MMPIO - multiSectorPIOWithMultipleMode
            SPIO - singleSectorPIOOnly
            ATA28 - ata28BitOnly
            NOMMPIO - noMultipleModeCommands
            MPTXFER - maxTransferLength (bytes)
            TPID - tpsiu on identify (limited use tpsiu)
            NCHK - do not use check condition bit at all
            CHKE - accepts the check condition bit, but returns empty data
            ATANVEMU - ata/nvme emulation (likely only realtek's chip right now). If this is set, the NVMe emulation
            will set basically only MN, SN, FWrev. DMA not supported, etc. Basically need to treat this as SCSI except
            for reading this data at this time.-TJE
            //Add more hacks below as needed to workaround other weird behavior for ATA passthrough.
            */
            bool smartCommandTransportWithSMARTLogCommandsOnly; // for USB adapters that hang when sent a GPL command to
                                                                // SCT logs, but work fine with SMART log commands
            // bool useA1SATPassthroughWheneverPossible;//For USB adapters that will only process 28bit commands with A1
            // and will NOT issue them with 85h
            bool a1NeverSupported;         // prevent retrying with 12B command since it isn't supported anyways.
            bool a1ExtCommandWhenPossible; // If this is set, when issuing an EXT (48bit) command, use the A1 opcode as
                                           // long as there are not ext registers that MUST be set to issue the command
                                           // properly. This is a major hack for devices that don't support the 85h
                                           // opcode.
            bool returnResponseInfoSupported;   // can send the SAT command to get response information for RTFRs
            bool returnResponseInfoNeedsTDIR;   // supports return response info, but must have T_DIR bit set for it to
                                                // work properly
            bool returnResponseIgnoreExtendBit; // Some devices support returning response info, but don't properly set
                                                // the extend bit, so this basically means copy extended RTFRs anyways.
            bool alwaysUseTPSIUForSATPassthrough; // some USBs do this better than others.
            bool alwaysCheckConditionAvailable;   // Not supported by all SAT translators. Don't set unless you know for
                                                  // sure!!!
            bool alwaysUseDMAInsteadOfUDMA; // send commands with DMA mode instead of UDMA since the device doesn't
                                            // support UDMA passthrough modes.
            bool dmaNotSupported;           // DMA passthrough is not available of any kind.
            bool partialRTFRs; // This means only 28bit RTFRs will be able to be retrived by the device. This hack is
                               // more helpful for code trying different commands to filter capabilities than for trying
                               // to talk to the device.
            bool noRTFRsPossible; // This means on command responses, we cannot get any return task file registers back
                                  // from the device, so avoid commands that rely on this behavior
            bool multiSectorPIOWithMultipleMode; // This means that multisector PIO works, BUT only when a set multiple
                                                 // mode command has been sent first and it is limited to the multiple
                                                 // mode.
            bool singleSectorPIOOnly; // This means that the adapter only supports single sector PIO transfers
            bool ata28BitOnly; // This is for some devices where the passthrough only allows a 28bit command through,
                               // even if the target drive is 48bit
            bool noMultipleModeCommands; // This is to disable use read/write multiple commands if a bridge chip doesn't
                                         // handle them correctly.
            // uint8_t reserved[1];//padd byte for 8 byte boundary with above bools.
            uint32_t maxTransferLength; // ATA Passthrough max transfer length in bytes. This may be different than the
                                        // scsi translation max.
            bool limitedUseTPSIU; // This might work for certain other commands, but only identify device has been found
                                  // to show this. Using TPSIU on identify works as expected, but other data transfers
                                  // abort this.
            bool disableCheckCondition; // Set when check condition bit cannot be used because it causes problems
            bool checkConditionEmpty;   // Accepts the check condition bit, but returns empty data.
            bool possilbyEmulatedNVMe; // realtek's USB to M.2 adapter can do AHCI or NVMe. Since nothing changes in IDs
                                       // and it emulates ATA identify data, need this to work around how it reports.
                                       // -TJE
        } ataPTHacks;
        // NVMe Hacks
        struct
        {
            // This is here mostly for vendor unique NVMe passthrough capabilities.
            // This structure may also be useful for OSs that have limited capabilities
            /* This comment describes the translation of fields from openSeaChest_PassthroughTest to the hacks in the
            structure below LIMPT - limitedPassthroughCapabilities IDGLP - this means setting the bools for identify and
            getLotpage to true, but none of the other commands.
            TODO: This part of the passthrough test is far from complete. Many of the listed commands are based on
            looking at limited IOCTLs in some OSs or other documentation that indicates not all commands are possible in
            a given OS. These were added to ensure it is easier to add support in the future, but are not fully
            implemented at this time. Some of the limited commands are setup properly in Windows 10 vs Windows PE/RE
            since there are different capabilities between these OS configurations as documented by MSFT.
            */
            bool limitedPassthroughCapabilities; // If this is set to true, this means only certain commands can be
                                                 // passed through to the device. (See below struct, only populated when
                                                 // this is true, otherwise assume all commands work)
            struct
            { // This structure will hold which commands are available to passthrough if the above
              // "limitedPassthroughCapabilities" boolean is true, otherwise this structure should be ignored.
                bool identifyGeneric; // can "generically" send any identify command with any cns value. This typically
                                      // means any identify can be sent, not just controller and namespace. Basically
                                      // CNS field is available.
                bool identifyController;
                bool identifyNamespace;
                bool getLogPage;
                bool format;
                bool getFeatures;
                bool firmwareDownload;
                bool firmwareCommit;
                bool vendorUnique;
                bool deviceSelfTest;
                bool sanitize; // any sanitize command
                bool namespaceManagement;
                bool namespaceAttachment;
                bool setFeatures; // this does not have granularity for which features at this time!!!
                bool miSend;
                bool miReceive;
                bool securitySend;
                bool securityReceive;
                bool formatUserSecureErase;   // format with ses set to user erase
                bool formatCryptoSecureErase; // format with ses set to crypto erase
                bool sanitizeCrypto;          // Sanitize crypto erase is supported
                bool sanitizeBlock;           // Sanitize block erase is supported
                bool sanitizeOverwrite;       // Sanitize overwrite erase is supported
                // As other passthroughs are learned with different capabilities, add other commands that ARE supported
                // by them here so that other layers of code can know what capabilities a given device has.
            } limitedCommandsSupported;
            // uint8_t reserved[1];//padd out above bools to 8 byte boundaries
            uint32_t maxTransferLength;
            uint32_t nvmepadding; // padd 4 more bytes after transfer length to keep 8 byte boundaries
        } nvmePTHacks;
    } passthroughHacks;

    typedef struct s_driveInfo
    {
        eMediaType       media_type;
        eDriveType       drive_type;
        eInterfaceType   interface_type;
        eZonedDeviceType zonedType;          // most drives will report ZONED_TYPE_NOT_ZONED
        uint32_t         deviceBlockSize;    // This is the logical block size reported by the drive
        uint32_t         devicePhyBlockSize; // This is the physical block size reported by the drive.
        uint32_t         dataTransferSize;   // this is obsolete! Do not use this
        uint16_t sectorAlignment; // This will usually be set to 0 on newer drives. Older drives may set this alignment
                                  // differently
        uint8_t  padd0[2];
        uint64_t deviceMaxLba;
        char     serialNumber[SERIAL_NUM_LEN + 1];
        uint8_t  padd1[7];
        char     T10_vendor_ident[T10_VENDOR_ID_LEN + 1];
        uint8_t  padd2[7];
        char     product_identification[MODEL_NUM_LEN + 1]; // not INQ
        uint8_t  padd3[7];
        char     product_revision[FW_REV_LEN + 1];
        uint8_t  padd4[7];
        uint64_t worldWideName;
        union
        {
            uint8_t raw[8192];
            tAtaIdentifyData
                ata; // NOTE: This will automatically be byte swapped when saved here on big-endian systems for
                     // compatibility will all kinds of bit checks of the data throughout the code at this time. Use a
                     // separate buffer if you want the completely raw data without this happening. - TJE
            nvmeIdentifyData nvme;
            // reserved field below is set to 8192 because nvmeIdentifyData structure holds both controller and
            // namespace data which are 4k each
            uint8_t
                reserved[8192]; // putting this here to allow some compatibility when NVMe passthrough is NOT enabled.
        } IdentifyData;         // THis MUST be at an even 8 byte offset to be accessed correctly!!!
        tVpdData      scsiVpdData;      // Intentionally not part of the above IdentifyData union
        ataReturnTFRs lastCommandRTFRs; // This holds the RTFRs for the last command to be sent to the device. This is
                                        // not necessarily the last function called as functions may send multiple
                                        // commands to the device.
        struct
        {
            bool    validData; // must be true for any other fields to be useful
            uint8_t senseKey;
            uint8_t additionalSenseCode;
            uint8_t additionalSenseCodeQualifier;
            uint8_t padd[4];
        } ataSenseData;
        uint8_t lastCommandSenseData[SPC3_SENSE_LEN]; // This holds the sense data for the last command to be sent to
                                                      // the device. This is not necessarily the last function called as
                                                      // functions may send multiple commands to the device.
        bool dpoFUAvalid;
        bool dpoFUA; // for use in cmds.h read/write functions. May be useful elsewhere too. This is not initialized by
                     // fill drive info at this time. It is populated when calling read_LBA or write_LBA.
        uint8_t padd5[2];
        struct
        {
            uint32_t lastNVMeCommandSpecific; // DW0 of command completion. Not all OS's return this so it is not always
                                              // valid...only really useful for SNTL when it is used. Linux, Solaris,
                                              // FreeBSD, UEFI. Windows is the problem child here.
            uint32_t lastNVMeStatus;          // DW3 of command completion. Not all OS's return this so it is not always
                                     // valid...only really useful for SNTL when it is used. Linux, Solaris, FreeBSD,
                                     // UEFI. Windows is the problem child here.
        } lastNVMeResult;
        bridgeInfo  bridge_info;
        adapterInfo adapter_info;
        driverInfo  driver_info;
        ataOptions  ata_Options;
        uint64_t    lastCommandTimeNanoSeconds; // The time the last command took in nanoseconds
        softwareSATFlags
            softSATFlags; // This is used by the software SAT translation layer. DO NOT Update this directly. This
                          // should only be updated by the lower layers of opensea-transport.
        uint32_t defaultTimeoutSeconds; // If this is not set (set to zero), a default value of 15 seconds will be used.
        uint8_t  padd6[4];
        union
        {
            uint32_t namespaceID; // This is the current namespace you are talking with. If this is zero, then this
                                  // value is invalid. This may not be available on all OS's or driver interfaces
            uint32_t lun;         // Logical unit number for SCSI. Not currently populated.
        };
        uint8_t currentProtectionType; // Useful for certain operations. Read in readCapacityOnSCSI. TODO: NVMe
        uint8_t piExponent;            // Only valid for protection types 2 & 3 I believe...-TJE
        uint8_t scsiVersion; // from STD Inquiry. Can be used elsewhere to help filter capabilities. NOTE: not an exact
                             // copy for old products where there was also EMCA and ISO versions. Set to ANSI version
                             // number in those cases.
        union
        {
            uint32_t numberOfLUs;        // number of logical units on the device
            uint32_t numberOfNamespaces; // number of namespaces on the controller
        };
        // 9304 bytes to make divisible by 8
        passthroughHacks passThroughHacks;
    } driveInfo;

#if defined(UEFI_C_SOURCE)
    typedef enum eUEFIPassthroughTypeEnum
    {
        UEFI_PASSTHROUGH_UNKNOWN,
        UEFI_PASSTHROUGH_SCSI,
        UEFI_PASSTHROUGH_SCSI_EXT,
        UEFI_PASSTHROUGH_ATA,
        UEFI_PASSTHROUGH_NVME,
    } eUEFIPassthroughType;
#endif

#if defined(_WIN32) && !defined(UEFI_C_SOURCE)
    // These are here instead of a Windows unique file due to messy includes.
    // We should be able to find a better way to handle this kind of OS unique stuff in the future.
    typedef enum eWindowsIOCTLTypeEnum
    {
        WIN_IOCTL_NOT_SET = 0,         // this should only be like this when allocating memory...
        WIN_IOCTL_ATA_PASSTHROUGH,     // Set when using ATA Pass-through IOCTLs (direct and double buffered)
        WIN_IOCTL_SCSI_PASSTHROUGH,    // Set when using SCSI Pass-through IOCTLs (direct and double buffered)
        WIN_IOCTL_SCSI_PASSTHROUGH_EX, // Set when using SCSI Pass-through IOCTLs (direct and double buffered) //Win 8
                                       // and newer only! You can set this to force using the EX passthrough in Win8,
                                       // BUT not many drivers support this and it will automatically be used for 32
                                       // byte CDBs when supported...that's the only real gain right now. - TJE
        WIN_IOCTL_SMART_ONLY, // Only the legacy SMART command IOCTL is supported, so only ATA identify and SMART
                              // commands are available
        WIN_IOCTL_IDE_PASSTHROUGH_ONLY, // Only the old & undocumented IDE pass-through is supported. Do not use this
                                        // unless absolutely nothing else works.
        WIN_IOCTL_SMART_AND_IDE, // Only the legacy SMART and IDE IOCTLs are supported, so 28bit limitations abound
        WIN_IOCTL_STORAGE_PROTOCOL_COMMAND, // Win10 + only. Should be used with NVMe. Might work with SCSI or ATA, but
                                            // that is unknown...development hasn't started for this yet. Just a
                                            // placeholder - TJE
        WIN_IOCTL_BASIC, // Very basic and does no real pass-through commands. It just reports enough data to keep
                         // things more or less "happy". Calling into other MSFT calls to do everything necessary. SCSI
                         // only device type at this time. - TJE
    } eWindowsIOCTLType;

    typedef enum eWindowsIOCTLMethodEnum
    {
        WIN_IOCTL_DEFAULT_METHOD,
        WIN_IOCTL_FORCE_ALWAYS_DIRECT,
        WIN_IOCTL_FORCE_ALWAYS_DOUBLE_BUFFERED,
        WIN_IOCTL_MAX_METHOD
    } eWindowsIOCTLMethod;
#endif // defined (_WIN32) && !defined(UEFI_C_SOURCE)

#if defined(_AIX)
    // AIX specific structures/enums/etc
    typedef enum eAIXPassthroughTypeEnum
    {
        AIX_PASSTHROUGH_NOT_SET = 0,
        AIX_PASSTHROUGH_SCSI,
        AIX_PASSTHROUGH_IDE_ATA,
        AIX_PASSTHROUGH_IDE_ATAPI,
        AIX_PASSTHROUGH_SATA,
        AIX_PASSTHROUGH_NVME,
    } eAIXPassthroughType;

    typedef enum eAIXAdapterTypeEnum
    {
        AIX_ADAPTER_UNKNOWN = 0,
        AIX_ADAPTER_SCSI, // as in parallel SCSI
        AIX_ADAPTER_IDE,
        AIX_ADAPTER_SAS,
        AIX_ADAPTER_SATA,
        AIX_ADAPTER_FC, // fibre channel
        AIX_ADAPTER_USB,
        AIX_ADAPTER_VSCSI,
        AIX_ADAPTER_ISCSI,
        AIX_ADAPTER_DASD,
        AIX_ADAPTER_NVME,
    } eAIXAdapterType;
#endif //_AIX

    // forward declare csmi info to avoid including csmi_helper.h
    typedef struct s_csmiDeviceInfo csmiDeviceInfo, *ptrCsmiDeviceInfo;

    static M_INLINE void safe_free_csmi_dev_info(csmiDeviceInfo** csmidevinfo)
    {
        safe_free_core(M_REINTERPRET_CAST(void**, csmidevinfo));
    }

    // forward declare cciss device
    typedef struct s_cissDeviceInfo cissDeviceInfo, *ptrCissDeviceInfo;

    static M_INLINE void safe_free_ciss_dev_info(cissDeviceInfo** cissdevinfo)
    {
        safe_free_core(M_REINTERPRET_CAST(void**, cissdevinfo));
    }

#define OS_HANDLE_NAME_MAX_LENGTH          256
#define OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH 24
#define OS_SECOND_HANDLE_NAME_LENGTH       30
    // \struct typedef struct s_OSDriveInfo
    typedef struct s_OSDriveInfo
    {
        char name[OS_HANDLE_NAME_MAX_LENGTH];                  // handle name (string)
        char friendlyName[OS_HANDLE_FRIENDLY_NAME_MAX_LENGTH]; // Handle name in a shorter/more friendly format.
                                                               // Example: name=\\.\PHYSICALDRIVE0 friendlyName=PD0
        eOSType osType;                                        // useful for lower layers to do OS specific things
        uint8_t minimumAlignment; // This is a power of 2 value representing the byte alignment required. 0 - no
                                  // requirement, 1 - single byte alignment, 2 - word, 4 - dword, 8 - qword, 16 - 128bit
                                  // aligned
        uint8_t padd0[3];
#if defined(UEFI_C_SOURCE)
        EFI_HANDLE   fd;
        EFI_DEV_PATH devicePath; // This type being used is a union of all the different possible device paths. - This
                                 // is 48 bytes
        eUEFIPassthroughType passthroughType;
        union _uefiAddress
        {
            struct _scsiAddress
            {
                uint32_t target;
                uint64_t lun;
            } scsi;
            struct _scsiExtAddress
            {
                uint8_t  target[TARGET_MAX_BYTES];
                uint64_t lun;
            } scsiEx;
            struct _ataAddress
            {
                uint16_t port;
                uint16_t portMultiplierPort;
            } ata;
            struct _nvmeAddress
            {
                uint32_t namespaceID;
            } nvme;
            uint8_t raw[24];
        } address;
        uint16_t controllerNum; // used to figure out which controller the above address applies to.
        uint8_t  paddUEFIAddr[2];
#elif defined(__linux__)
#    if defined(VMK_CROSS_COMP)
    /**
     * In VMWare we discover or send IOCTL to NVMe throught NDDK.
     * So we will need 2 different handle for NVMe_IO and SG_IO
     *
     * @author 521852 (8/27/2018)
     */
    int                 fd;
    struct nvme_handle* nvmeFd;
#    else
    int fd; // primary handle
#    endif
    bool scsiAddressValid; // will be true if the SCSI address is a valid address
    struct
    {
        uint8_t host;    // AKA SCSI adapter #
        uint8_t channel; // AKA bus
        uint8_t target;  // AKA id number
        uint8_t lun;     // logical unit number
    } scsiAddress;
    bool secondHandleValid; // must be true for remaining fields to be used.
    char secondName[OS_SECOND_HANDLE_NAME_LENGTH];
    char secondFriendlyName[OS_SECOND_HANDLE_NAME_LENGTH];
    bool secondHandleOpened;
#    if defined(VMK_CROSS_COMP)
    /**
     * In VMWare we discover or send IOCTL to NVMe throught NDDK.
     * So we will need 2 different handle for NVMe_IO and SG_IO
     *
     * @author 521852 (8/27/2018)
     */
    int                 fd2;
    struct nvme_handle* nvmeFd2;
#    else
    int fd2; // secondary handle. Ex: fd = sg handle opened, fd2 = sd handle opened.
#    endif
    struct
    {
        bool    driverVersionValid;
        uint8_t majorVersion;
        uint8_t minorVersion;
        uint8_t revision;
    } sgDriverVersion;
#    if defined(VMK_CROSS_COMP)
    uint8_t paddSG[35]; // TODO: need to change this based on size of NVMe handle for VMWare.
#    else
    uint8_t paddSG[35];
#    endif
#elif defined(_WIN32)
    HANDLE fd;
    HANDLE scsiSRBHandle; // To support for SCSI SRB IOCTLs (miniport) that use this same handle type
                          // (\\.\SCSI<pathId>:)
    SCSI_ADDRESS  scsi_addr;
    uint32_t      os_drive_number;
    int           srbtype; // this will be used to filter when a controller supports the new SCSI PassThrough EX IOCTLs
    unsigned long alignmentMask; // save the alignment mask. This may be needed on some controllers....not currently
                                 // used but SHOULD be added later for the SCSI IOCTL DIRECT EX
    eWindowsIOCTLType ioType;    // This will be set during get_Device so we know how to talk to the drive (Mostly for
                                 // ATA). Only change this if you know what you're doing.
    eWindowsIOCTLMethod
        ioMethod; // Use this to force using DIRECT or Double Buffered IOCTLs for each command. By default the library
                  // will decide...typically 16KiB or less will use double buffered for compatibility purposes. This is
                  // ignored for IDE and SMART IOCTLs since they are only double buffered.
    struct
    {
        bool
            smartIOSupported; // if this is false, nothing below this is valid. This just tracks whether the SMART IO is
                              // available or not. it will only be set when other ATA Pass-through methods fail. - TJE
        bool    ataIDsupported;   // EC command can be sent through this IO
        bool    atapiIDsupported; // A1 command can be sent through this IO
        bool    smartSupported;   // B0 command can be sent through this IO
        uint8_t deviceBitmap; // This specifies which channel the drive is on (PATA)...might need this for sending this
                              // IO on some legacy systems. See bIDEDeviceMap here
                              // https://msdn.microsoft.com/en-us/library/windows/hardware/ff554977(v=vs.85).aspx
    } winSMARTCmdSupport;
    struct
    {
        bool fwdlIOSupported;
        bool
            allowFlexibleUseOfAPI; // Set this to true to allow using the Win10 API for FWDL for any compatible download
                                   // commands. If this is false, the Win10 API will only be used on IDE_INTERFACE for
                                   // an ATA download command and SCSI interface for a supported Write buffer command.
                                   // If true, it will be used regardless of which command the caller is using. This is
                                   // useful for pure FW updates versus testing a specific condition.
        uint32_t
            payloadAlignment; // From MSDN: The alignment of the image payload, in number of bytes. The maximum is
                              // PAGE_SIZE. The transfer size is a mutliple of this size. Some protocols require at
                              // least sector size. When this value is set to 0, this means that this value is invalid.
        uint32_t maxXferSize; // From MSDN: The image payload maximum size, this is used for a single command
        // expand this struct if we need other data when we check for firmware download support on a device.
    } fwdlIOsupport;
    uint32_t adapterMaxTransferSize; // Bytes. Returned by querying for adapter properties. Can be used to know when
                                     // trying to request more than the adapter or driver supports.
    bool openFabricsNVMePassthroughSupported; // If true, then nvme commands can be issued using the open fabrics NVMe
                                              // passthrough IOCTL
    bool intelNVMePassthroughSupported; // if true, this is a device that supports intel's nvme passthrough, but doesn't
                                        // show up as full features with CSMI as expected otherwise.
    bool fwdlMiniportSupported;   // Miniport IOCTL for FWDL is supported. This should be in the structure above, but it
                                  // is here for compatibility at this time - TJE
    HANDLE   forceUnitAccessRWfd; // used for os_read and os_Write when using the force unit access option
    uint32_t volumeBitField; // This is a bitfield that is stored to prevent rereading, mounting, waking all systems on
                             // the system. Since we read this up front, this will be stored so taht each partition on a
                             // device can be unmouted later if necessary. - TJE
    uint8_t adapterDescBusType; // bus type reported in adapter descriptor
    uint8_t deviceDescBusType;  // bus type reported in the device descriptor
// TODO: Store the device path! This may occasionally be useful to have. Longest one will probably be no more that
// MAX_DEVICE_ID_LEN characters. (This is defined as 200) padding to keep same size as other OSs. This is to keep things
// similar across OSs. Variable sizes based on 32 vs 64bit since handle is a void*
#    if defined(_WIN64)
    uint8_t paddWin[32];
#    else
    uint8_t paddWin[44];
#    endif // Win64 for padding
#elif defined(__FreeBSD__)
    int fd; // used when cam is not being used (legacy ATA or NVMe IO without CAM....which may not be supported, but
            // kept here just in case)
    struct cam_device* cam_dev; // holds fd inside for CAM devices among other information
#    if defined(__x86_64__) || defined(__amd64__) || defined(__aarch64__) || defined(__ia64__) ||                      \
        defined(__itanium__) || defined(__powerpc64__) || defined(__ppc64__) || defined(__spark__)
    uint8_t freeBSDPadding[102]; // padding on 64bit OS
#    else
    uint8_t freeBSDPadding[106]; // padding on 32bit OS
#    endif
#elif defined(_AIX)
    int  fd;     // rhdisk handle
    int  ctrlfd; // handle to the controller (required for NVMe, may not be used for SCSI/SATA)
    bool ctrlfdValid;
    bool diagnosticModeFlagInUse; // handle was opened with the diagnostic mode flag set, which allows some other IOCTLs
                                  // which require this flag-TJE
    uint64_t scsiID;
    uint64_t lunID; // nvme namespace for NVME devices-TJE
    eAIXPassthroughType
        ptType; // used to route the command to the correct passthrough for the device/controller combination
    eAIXAdapterType
             adapterType; // can be helpful as there are some minor differences in required fields between adapter types
    uint32_t maxXferLength;  // maximum transfer length that was reported by the controller
    uint8_t  aixPadding[76]; // padding the structure out to keep same size as other OSs
#else                                // OS preprocessor checks
    int     fd; // some other nix system that only needs a integer file handle
    uint8_t otherPadd[110];
#endif                               // OS preprocessor checks
        bool osReadWriteRecommended; // This will be set to true when it is recommended that OS read/write calls are
                                     // used instead of IO read/write (typically when using SMART or IDE IOCTLs in
                                     // Windows since they may not work right for read/write)
#if defined(_WIN32)
        DWORD last_error; // GetLastError in Windows.
#else
    errno_t last_error; // errno in Linux
#endif
        struct
        {
            bool fileSystemInfoValid; // This must be set to true for the other bools to have any meaning. This is here
                                      // because some OS's may not have support for detecting this information
            union
            {
                bool hasFileSystem; //[deprecated], use the hasActiveFileSystem below. This will only be true for
                                    // filesystems the current OS can detect. Ex: Windows will only set this for mounted
                                    // volumes it understands (NTFS, FAT32, etc). Linux may set this for more filesystem
                                    // types since it can handle more than Windows by default
                bool hasActiveFileSystem; // This is a bit more clear that the filesystem detected was mounted and is in
                                          // use within the OS.
            };
            bool isSystemDisk; // This will be set if the drive has a file system and the OS is running off of it. Ex:
                               // Windows' C:\Windows\System32, Linux's / & /boot, etc
        } fileSystemInfo;
        ptrCsmiDeviceInfo
            csmiDeviceData; // This is a pointer because it will only be allocated when CSMI is supported. This is also
                            // used by Intel RST NVMe passthrough which is basically an extension of CSMI
        ptrCissDeviceInfo cissDeviceData; // This pointer is allocated only when CCISS is supported.
        uint8_t           padd[6];        // padd to multiple of 8 bytes
    } OSDriveInfo;

#define DEFAULT_DISCOVERY  0
#define FAST_SCAN          1 // Gets the basic information for a quick scan like SeaChest displays on the command line.
#define DO_NOT_WAKE_DRIVE  2 // e.g OK to send commands that do NOT access media
#define NO_DRIVE_CMD       3
#define OPEN_HANDLE_ONLY   4
#define BUS_RESCAN_ALLOWED BIT15 // this may wake the drive!
    // Flags below are bitfields...so multiple can be set. Flags above should be checked by only checking the first word
    // of this enum.
#define FORCE_ATA_PIO_ONLY                                                                                             \
    BIT16 // troubleshooting option to only send PIO versions of commands (used in get_Device/fill_Drive_Info).
#define FORCE_ATA_DMA_SAT_MODE                                                                                         \
    BIT17 // troubleshooting option to send all DMA commands with protocol set to DMA in SAT CDBs
#define FORCE_ATA_UDMA_SAT_MODE                                                                                        \
    BIT18 // troubleshooting option to send all DMA commands with protocol set to DMA in SAT CDBs
#define GET_DEVICE_FUNCS_IGNORE_CSMI                                                                                   \
    BIT19 // use this bit in get_Device_Count and get_Device_List to ignore CSMI devices.
#define GET_DEVICE_FUNCS_VERBOSE_COMMAND_NAMES   BIT20 // matches v2
#define GET_DEVICE_FUNCS_VERBOSE_COMMAND_VERBOSE BIT21 // matches v3
#define GET_DEVICE_FUNCS_VERBOSE_BUFFERS         BIT22 // matches v4

    typedef eReturnValues (*issue_io_func)(void*);

#define DEVICE_BLOCK_VERSION (9)

    // verification for compatibility checking
    typedef struct s_versionBlock
    {
        uint32_t size;    // size of enclosing structure
        uint32_t version; // version of enclosing structure
    } versionBlock;

    // \struct typedef struct _tDevice
    typedef struct s_tDevice
    {
        versionBlock  sanity;
        OSDriveInfo   os_info;
        driveInfo     drive_info;
        void*         raid_device;
        issue_io_func issue_io; // scsi IO function pointer for raid or other driver/custom interface to send commands
        issue_io_func
                 issue_nvme_io; // nvme IO function pointer for raid or other driver/custom interface to send commands
        uint64_t dFlags;
        eVerbosityLevels deviceVerbosity;
        uint32_t         delay_io;
    } tDevice;

    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RW(1)
    M_PARAM_RO(2) static M_INLINE void copy_ata_identify_to_tdevice(tDevice* device, uint8_t* identifyData)
    {
        DISABLE_NONNULL_COMPARE
        if (device != M_NULLPTR && identifyData != M_NULLPTR &&
            M_REINTERPRET_CAST(uintptr_t, &device->drive_info.IdentifyData.ata) !=
                M_REINTERPRET_CAST(uintptr_t, identifyData))
        {
            safe_memcpy(M_REINTERPRET_CAST(void*, &device->drive_info.IdentifyData.ata), 512, identifyData, 512);
        }
        RESTORE_NONNULL_COMPARE
    }

    // Common enum for getting/setting power states.
    // This enum encompasses Mode Sense/Select commands for SCSI, Set Features for ATA
    // And Get/Set Features for NVMe. Lower layers must translated bits according to interface.
    typedef enum eFeatureModeSelectEnum
    {
        CURRENT_VALUE,
        CHANGEABLE_VALUE,
        DEFAULT_VALUE,
        SAVED_VALUE,
        CAPABILITIES
    } eFeatureModeSelect;

    // this enum is used to know which power conditions we are interested in...it is here so that both ATA and SCSI can
    // see it
    typedef enum ePowerConditionIDEnum
    {
        PWR_CND_NOT_SET   = -1,
        PWR_CND_STANDBY_Z = 0x00, // value according to ATA spec.
        PWR_CND_STANDBY_Y = 0x01, // value according to ATA spec.
        PWR_CND_IDLE_A    = 0x81, // value according to ATA spec.
        PWR_CND_IDLE_B    = 0x82, // value according to ATA spec.
        PWR_CND_IDLE_C    = 0x83, // value according to ATA spec.
        PWR_CND_ALL       = 0xFF, // value according to ATA spec.
        PWR_CND_ACTIVE, // Not defined in ATA, but another power mode that may be specified, so it is placed after the
                        // defined ATA values.
        PWR_CND_IDLE,   // Legacy idle mode. Basically the same as idle_a, but defined separately since a different
                        // command may be used to transition to this mode.
        PWR_CND_IDLE_UNLOAD, // sending the idle immediate - unload option
        PWR_CND_STANDBY,     // Legacy Standby mode. Basically the same as standby_z, but defined separately since a
                             // different command may be used to transition to this mode.
        PWR_CND_SLEEP, // Sleep mode. WARNING: This require a reset to wake up from, but it is included here for those
                       // that want to do this
        PWR_CND_RESERVED
    } ePowerConditionID;

#define LEGACY_DRIVE_SEC_SIZE UINT16_C(512)
#define COMMON_4K_SIZE        UINT16_C(4096)
#define MAX_28_BIT_LBA        UINT32_C(0xFFFFFFF)
#define MAX_48_BIT_LBA        UINT64_C(0xFFFFFFFFFFFF)

    // the following link can be used to look-up and add additional OUIs
    // https://standards.ieee.org/develop/regauth/oui/public.html
    typedef enum eIEEE_OUIsEnum
    {
        IEEE_UNKNOWN             = 0,
        IEEE_MAXTOR              = 0x0010B9,
        IEEE_SEAGATE_SAMSUNG_HDD = 0x0004CF, // appears to be what Seagate Samsung drives have for OUI
        IEEE_SEAGATE_NVME        = 0x0004CF,
        IEEE_SEAGATE1            = 0x000C50,
        IEEE_SEAGATE2            = 0x0011C6,
        IEEE_SEAGATE3            = 0x0014C3,
        IEEE_SEAGATE4            = 0x001862,
        IEEE_SEAGATE5            = 0x001D38,
        IEEE_SEAGATE6            = 0x002037,
        IEEE_SEAGATE7            = 0x0024B6,
        IEEE_SEAGATE8            = 0xB45253,
        IEEE_SAMSUNG_SSD         = 0x002538,
        IEEE_SAMSUNG_HDD1        = 0x0000F0,
        IEEE_SAMSUNG_HDD2        = 0x0024E9,
        IEEE_VENDOR_A            = 0x00242F,
        IEEE_VENDOR_A_TECHNOLOGY = 0x00A075,
    } eIEEE_OUIs;

    // http://www.linux-usb.org/usb.ids
    typedef enum eUSBVendorIDsEnum
    {
        USB_Vendor_Unknown                            = 0,
        USB_Vendor_Adaptec                            = 0x03F3,
        USB_Vendor_Buffalo                            = 0x0411,
        USB_Vendor_Seagate                            = 0x0477,
        USB_Vendor_Integrated_Techonology_Express_Inc = 0x048D,
        USB_Vendor_Samsung                            = 0x04E8,
        USB_Vendor_Sunplus                            = 0x04FC,
        USB_Vendor_Alcor_Micro_Corp                   = 0x058F,
        USB_Vendor_LaCie                              = 0x059F,
        USB_Vendor_GenesysLogic                       = 0x05E3,
        USB_Vendor_Prolific                           = 0x067B,
        USB_Vendor_SanDisk_Corp                       = 0x0781,
        USB_Vendor_Silicon_Motion                     = 0x090C,
        USB_Vendor_Oxford                             = 0x0928,
        USB_Vendor_Seagate_RSS                        = 0x0BC2,
        USB_Vendor_Realtek                            = 0x0BDA,
        USB_Vendor_Maxtor                             = 0x0D49,
        USB_Vendor_Phison                             = 0x0D7D,
        USB_Vendor_Initio                             = 0x13FD,
        USB_Vendor_Kingston = 0x13FE, // Some online databases show patriot memory, and one also shows Phison. Most
                                      // recognize this as Kingston.
        USB_Vendor_JMicron          = 0x152D,
        USB_Vendor_ASMedia          = 0x174C,
        USB_Vendor_4G_Systems_GmbH  = 0x1955,
        USB_Vendor_SeagateBranded   = 0x1A2A,
        USB_Vendor_Symwave          = 0x1CA1,
        USB_Vendor_ChipsBank        = 0x1E3D,
        USB_Vendor_Via_Labs         = 0x2109,
        USB_Vendor_Dell             = 0x413C,
        USB_Vendor_Kingston_Generic = 0x8888,
        // Add new enumerations above this line!
        USB_Vendor_MaxValue = 0xFFFF
    } eUSBVendorIDs;

    typedef enum e1394OUIsEnum // a.k.a. vendor IDs
    {
        IEEE1394_Vendor_Unknown = 0,
        // IEEE1394_Vendor_Maxtor  = 0x001075,//This is a second Maxtor VID, but it is not listed as used, which is why
        // it is commented out
        IEEE1394_Vendor_Maxtor  = 0x0010B9,
        IEEE1394_Vendor_Seagate = 0x002037,
        IEEE1394_Vendor_Quantum = 0x00E09E,
        // Add new enumerations above this line!
        IEEE1394_Vendor_MaxValue =
            0xFFFFFF // this should be the the highest possible value for an IEEE OUI as they are 24bits in size.
    } e1394OUIs;     // a.k.a. vendor IDs

    // There are multiple resources that can be used to find these:
    // https://pcisig.com/membership/member-companies?combine=&order=field_vendor_id&sort=asc
    // https://www.pcilookup.com/
    // https://pci-ids.ucw.cz/
    // Not used at this time, but have added IDs that have been found in use for HBAs
    typedef enum ePCIVendorIDsEnum
    {
        PCI_VENDOR_UNKNOWN       = 0,
        PCI_VENDOR_LSI           = 0x1000, // now broadcom
        PCI_VENDOR_IBM           = 0x1014,
        PCI_VENDOR_AMD           = 0x1022,
        PCI_VENDOR_HP            = 0x103C,
        PCI_VENDOR_SILICON_IMAGE = 0x1095,
        PCI_VENDOR_HIGHPOINT     = 0x1103,
        PCI_VENDOR_MICROCHIP     = 0x11F8, // or PMC?
        PCI_VENDOR_SEAGATE       = 0x1BB1,
        PCI_VENDOR_3WARE         = 0x13C1,
        PCI_VENDOR_BROADCOM      = 0x14E4,
        PCI_VENDOR_HPE           = 0x1590,
        PCI_VENDOR_ARECA         = 0x17D3,
        PCI_VENDOR_JMICRON       = 0x197B,
        PCI_VENDOR_AVAGO         = 0x1A1F,
        PCI_VENDOR_RED_HAT       = 0x1AF4,
        PCI_VENDOR_ASMEDIA       = 0x1B21,
        PCI_VENDOR_MARVEL        = 0x1DCA,
        PCI_VENDOR_INTEL         = 0x8086,
        PCI_VENDOR_ADAPTEC       = 0x9004,
        PCI_VENDOR_ADAPTEC_2     = 0x9005,
    } ePCIVendorIDs;

    typedef enum eSeagateFamilyEnum
    {
        NON_SEAGATE      = 0,
        SEAGATE          = BIT1,
        MAXTOR           = BIT2,
        SAMSUNG          = BIT3,
        LACIE            = BIT4,
        SEAGATE_VENDOR_A = BIT5,
        SEAGATE_VENDOR_B = BIT6,
        SEAGATE_VENDOR_C = BIT7,
        SEAGATE_VENDOR_D = BIT8,
        SEAGATE_VENDOR_E = BIT9,
        // Ancient history
        SEAGATE_QUANTUM          = BIT10, // Quantum Corp. Vendor ID QUANTUM (SCSI)
        SEAGATE_CDC              = BIT11, // Control Data Systems. Vendor ID CDC (SCSI)
        SEAGATE_CONNER           = BIT12, // Conner Peripherals. Vendor ID CONNER (SCSI)
        SEAGATE_MINISCRIBE       = BIT13, // MiniScribe. Vendor ID MINSCRIB (SCSI)
        SEAGATE_DEC              = BIT14, // Digital Equipment Corporation. Vendor ID DEC (SCSI)
        SEAGATE_PRARIETEK        = BIT15, // PrarieTek. Vendor ID PRAIRIE (SCSI).
        SEAGATE_PLUS_DEVELOPMENT = BIT16, // Plus Development. Unknown detection
        SEAGATE_CODATA           = BIT17, // CoData. Unknown detection
        // Recently Added
        SEAGATE_VENDOR_F      = BIT18,
        SEAGATE_VENDOR_G      = BIT19,
        SEAGATE_VENDOR_H      = BIT20,
        SEAGATE_VENDOR_SSD_PJ = BIT21, // Older enterprise NVMe drives that had some unique capabilities
        SEAGATE_VENDOR_K      = BIT22,
    } eSeagateFamily;

    // The scan flags should each be a bit in a 32bit unsigned integer.
    //  bits 0:7 Will be used for drive type selection.
    //  bits 8:15 will be used for interface selection. So this is slightly different because if you say SCSI interface
    //  you can get back both ATA and SCSI drives if they are connected to say a SAS card Linux - bit 16 will be used to
    //  change the handle that shows up from the scan. Linux - bit 17 will be used to show the SD to SG mapping in
    //  linux. Windows - bit 16 will be used to show the long device handle name RAID interfaces (including csmi) may
    //  use bits 31:26 (so far those are the only ones used by CSMI)

#define DEFAULT_SCAN          0
#define ALL_DRIVES            0xFF
#define ATA_DRIVES            BIT0
#define USB_DRIVES            BIT1
#define SCSI_DRIVES           BIT2
#define NVME_DRIVES           BIT3
#define RAID_DRIVES           BIT4
#define ALL_INTERFACES        0xFF00
#define IDE_INTERFACE_DRIVES  BIT8
#define SCSI_INTERFACE_DRIVES BIT9
#define USB_INTERFACE_DRIVES  BIT10
#define NVME_INTERFACE_DRIVES BIT11
#define RAID_INTERFACE_DRIVES BIT12
#define SD_HANDLES            BIT16 // this is a Linux specific flag to show SDX handles instead of SGX handles
#define SG_TO_SD              BIT17
// #define SAT_12_BYTE BIT18
#define SCAN_SEAGATE_ONLY BIT19
#define AGRESSIVE_SCAN                                                                                                 \
    BIT20 // this can wake a drive up because a bus rescan may be issued. (currently only implemented in Windows)
#if defined(ENABLE_CSMI)
#    define ALLOW_DUPLICATE_DEVICE                                                                                     \
        BIT24 // This is ONLY used by the scan_And_Print_Devs function to filter what is output from it. This does NOT
              // affect get_Device_List.
#    define IGNORE_CSMI                                                                                                \
        BIT25 // only works in Windows since Linux never adopted CSMI support. Set this to ignore CSMI devices, or
              // compile opensea-transport without the ENABLE_CSMI preprocessor definition.
#endif
#define SCAN_IRONWOLF_NAS_ONLY BIT26
#define SCAN_SKYHAWK_EXOS_ONLY BIT27

    typedef enum eZoneReportingOptionsEnum
    {
        ZONE_REPORT_LIST_ALL_ZONES                             = 0x00,
        ZONE_REPORT_LIST_EMPTY_ZONES                           = 0x01,
        ZONE_REPORT_LIST_IMPLICIT_OPEN_ZONES                   = 0x02,
        ZONE_REPORT_LIST_EXPLICIT_OPEN_ZONES                   = 0x03,
        ZONE_REPORT_LIST_CLOSED_ZONES                          = 0x04,
        ZONE_REPORT_LIST_FULL_ZONES                            = 0x05,
        ZONE_REPORT_LIST_READ_ONLY_ZONES                       = 0x06,
        ZONE_REPORT_LIST_OFFLINE_ZONES                         = 0x07,
        ZONE_REPORT_LIST_INACTIVE_ZONES                        = 0x08,
        ZONE_REPORT_LIST_ZONES_WITH_RESET_SET_TO_ONE           = 0x10,
        ZONE_REPORT_LIST_ZONES_WITH_NON_SEQ_SET_TO_ONE         = 0x11,
        ZONE_REPORT_LIST_ALL_ZONES_EXCEPT_GAP_ZONES            = 0x3E,
        ZONE_REPORT_LIST_ALL_ZONES_THAT_ARE_NOT_WRITE_POINTERS = 0x3F
    } eZoneReportingOptions;

    typedef enum eZoneDomainReportingOptionsEnum
    {
        ZONE_DOMAIN_REPORT_ALL_ZONE_DOMAINS            = 0x00,
        ZONE_DOMAIN_REPORT_ALL_ZONES_ACTIVE            = 0x01,
        ZONE_DOMAIN_REPORT_CONTAIN_ACTIVE_ZONES        = 0x02,
        ZONE_DOMAIN_REPORT_DO_NOT_CONTAIN_ACTIVE_ZONES = 0x03,
    } eZoneDomainReportingOptions;

    typedef enum eRealmsReportingOptionsEnum
    {
        REALMS_REPORT_ALL_REALMS                                                    = 0x00,
        REALMS_REPORT_ALL_REALMS_CONTAIN_SEQUENTIAL_OR_BEFORE_REQUIRED_ACTIVE_ZONES = 0x01,
        REALMS_REPORT_SEQUENTIAL_WRITE_REQUIRED_ACTIVE_ZONES                        = 0x02,
        REALMS_REPORT_SEQUENTIAL_WRITE_PREFERRED_ACTIVE_ZONES                       = 0x03,
    } eRealmsReportingOptions;

    typedef enum eZMActionEnum
    {
        ZM_ACTION_REPORT_ZONES         = 0x00, // dma in-in
        ZM_ACTION_CLOSE_ZONE           = 0x01, // non data-out
        ZM_ACTION_FINISH_ZONE          = 0x02, // non data-out
        ZM_ACTION_OPEN_ZONE            = 0x03, // non data-out
        ZM_ACTION_RESET_WRITE_POINTERS = 0x04, // non data-out
        ZM_ACTION_SEQUENTIALIZE_ZONE   = 0x05, // non data-out
        ZM_ACTION_REPORT_REALMS        = 0x06, // dma in
        ZM_ACTION_REPORT_ZONE_DOMAINS  = 0x07, // dma in
        ZM_ACTION_ZONE_ACTIVATE        = 0x08, // dma in
        ZM_ACTION_ZONE_QUERY           = 0x09, // dma in
    } eZMAction;

    OPENSEA_TRANSPORT_API bool os_Is_Infinite_Timeout_Supported(void);

// NOTE: This is only possible in some OS's! If you request this and it's not supported, OS_TIMEOUT_TOO_LARGE is
// returned.
#define INFINITE_TIMEOUT_VALUE UINT32_MAX

// Below, we have nastyness in order to figure out maximum possible timeouts (these may be less than infinite in case
// you need to know a time that is NOT infinite)
#if defined(UEFI_C_SOURCE)
#    define MAX_CMD_TIMEOUT_SECONDS UINT32_MAX
#elif defined(_WIN32)
#    define MAX_CMD_TIMEOUT_SECONDS 108000
#elif defined(__linux__)
#    define MAX_CMD_TIMEOUT_SECONDS 4294967
#elif defined(__FreeBSD__)
#    define MAX_CMD_TIMEOUT_SECONDS 4294967
#elif defined(__sun)
#    define MAX_CMD_TIMEOUT_SECONDS 65535
#elif defined(_AIX)
#    define MAX_CMD_TIMEOUT_SECONDS                                                                                    \
        (UINT32_MAX - 1) /*NOTE: This may not be correct but the field that sets this is a uint32. Setting to 1 less   \
                            than infinite's current value -TJE*/
#else
#    error "Need to set MAX_CMD_TIMEOUT_SECONDS for this OS"
#endif

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
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_WO(1) OPENSEA_TRANSPORT_API eReturnValues get_Opensea_Transport_Version(apiVersionInfo* ver);

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
    M_NONNULL_PARAM_LIST(1) M_PARAM_WO(1) OPENSEA_TRANSPORT_API eReturnValues get_Version_Block(versionBlock* ver);

    OPENSEA_TRANSPORT_API bool validate_Device_Struct(versionBlock sanity);

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
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_NULL_TERM_STRING(1)
    M_PARAM_RW(2) OPENSEA_TRANSPORT_API eReturnValues get_Device(const char* filename, tDevice* device);

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
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RW(1) OPENSEA_TRANSPORT_API eReturnValues get_Device_Count(uint32_t* numberOfDevices, uint64_t flags);

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
    //!   \return SUCCESS - pass, WARN_NOT_ALL_DEVICES_ENUMERATED - some deviec had trouble being enumerated. Validate
    //!   that it's drive_type is not UNKNOWN_DRIVE, !SUCCESS fail or something went wrong
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RW(1)
    OPENSEA_TRANSPORT_API eReturnValues get_Device_List(tDevice* const ptrToDeviceList,
                                                        uint32_t       sizeInBytes,
                                                        versionBlock   ver,
                                                        uint64_t       flags);

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
    M_NONNULL_PARAM_LIST(1) M_PARAM_RW(1) OPENSEA_TRANSPORT_API eReturnValues close_Device(tDevice* device);

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
    //!   \param[in] scanVerbosity = the verbosity to run the scan at
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    OPENSEA_TRANSPORT_API void scan_And_Print_Devs(unsigned int flags, eVerbosityLevels scanVerbosity);

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
    //!   \return size_t bytes read
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RO(1)
    M_NULL_TERM_STRING(1)
    M_PARAM_WO_SIZE(2, 3) OPENSEA_TRANSPORT_API size_t load_Bin_Buf(const char* filename, void* myBuf, size_t bufSize);

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
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) bool scan_Drive_Type_Filter(tDevice* device, uint32_t scanFlags);

    //-----------------------------------------------------------------------------
    //
    //  scan_Drive_Type_Filter()
    //
    //! \brief   Description:  This is a filter function used by scan to determine which drive interfaces found we want
    //! to show
    //
    //  Entry:
    //!   \param[in] device - pointer to the device structure that has already been filled in by get_Device()
    //!   \param[in] scanFlags = flags used to control scan. All flags that don't pertain to drive interface are
    //!   ignored.
    //!
    //  Exit:
    //!   \return true = show drive, false = don't show drive
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) bool scan_Interface_Type_Filter(tDevice* device, uint32_t scanFlags);

    //-----------------------------------------------------------------------------
    //
    //  is_Seagate_Family( tDevice * device )
    //
    //! \brief   Checks if the device is a Seagate drive, a Samsung HDD drive, a Maxtor Drive, or a LaCie Drive. This
    //! should be used for features we want to allow only on Seagate Supported products (erase, etc).
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!
    //  Exit:
    //!   \return eSeagateFamily enum value. See enum for meanings
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API eSeagateFamily is_Seagate_Family(tDevice* device);

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
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) M_NULL_TERM_STRING(1) OPENSEA_TRANSPORT_API bool is_Seagate_MN(const char* string);

    //-----------------------------------------------------------------------------
    //
    //  is_Seagate_VendorID( tDevice * device )
    //
    //! \brief   Checks if the vendor ID for a device is Seagate or SEAGATE. Useful for determinging a SAS or USB drive
    //! is a seagate drive
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!
    //  Exit:
    //!   \return 1 = It is a Seagate Drive, 0 - Not a Seagate Drive
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_Seagate_VendorID(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  is_Seagate( tDevice * device )
    //
    //! \brief   Checks if the device is a Seagate drive
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!   \param[in]  USBchildDrive - set to true to check USB child drive information. if set to false, this will
    //!   automatically also check the child drive info (this is really just used for recursion in the function)
    //!
    //  Exit:
    //!   \return 1 = It is a Seagate Drive, 0 - Not a Seagate Drive
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_Seagate(tDevice* device, bool USBchildDrive);

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
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_LaCie(tDevice* device);

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
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) M_NULL_TERM_STRING(1) OPENSEA_TRANSPORT_API bool is_Samsung_String(const char* string);

    //-----------------------------------------------------------------------------
    //
    //  is_Samsung_HDD( tDevice * device )
    //
    //! \brief   Checks if the device is a Samsung HDD drive
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!   \param[in]  USBchildDrive - set to true to check USB child drive information. if set to false, this will
    //!   automatically also check the child drive info (this is really just used for recursion in the function)
    //!
    //  Exit:
    //!   \return 1 = It is a Samsung Drive, 0 - Not a Samsung Drive
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_Samsung_HDD(tDevice* device, bool USBchildDrive);

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
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) M_NULL_TERM_STRING(1) OPENSEA_TRANSPORT_API bool is_Maxtor_String(const char* string);

    //-----------------------------------------------------------------------------
    //
    //  is_Maxtor( tDevice * device )
    //
    //! \brief   Checks if the device is a Maxtor drive
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!   \param[in]  USBchildDrive - set to true to check USB child drive information. if set to false, this will
    //!   automatically also check the child drive info (this is really just used for recursion in the function)
    //!
    //  Exit:
    //!   \return 1 = It is a Maxtor Drive, 0 - Not a Maxtor Drive
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_Maxtor(tDevice* device, bool USBchildDrive);

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
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_Seagate_Model_Vendor_A(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  is_Vendor_A( tDevice * device )
    //
    //! \brief   Checks if the device is a Partner drive
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!   \param[in]  USBchildDrive - set to true to check USB child drive information. if set to false, this will
    //!   automatically also check the child drive info (this is really just used for recursion in the function)
    //!
    //  Exit:
    //!   \return 1 = It is a Partner Drive, 0 - Not a Partner Drive
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_Vendor_A(tDevice* device, bool USBchildDrive);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) M_NULL_TERM_STRING(1) OPENSEA_TRANSPORT_API bool is_Conner_Model_Number(const char* mn);

    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_Conner_VendorID(tDevice* device);

    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_Connor(tDevice* device, bool USBchildDrive);

    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_CDC_VendorID(tDevice* device);

    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_DEC_VendorID(tDevice* device);

    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_MiniScribe_VendorID(tDevice* device);

    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_Quantum_VendorID(tDevice* device);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) M_NULL_TERM_STRING(1) OPENSEA_TRANSPORT_API bool is_Quantum_Model_Number(const char* string);

    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_Quantum(tDevice* device, bool USBchildDrive);

    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_PrarieTek_VendorID(tDevice* device);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_Seagate_Model_Number_Vendor_B(tDevice* device, bool USBchildDrive);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_Seagate_Model_Number_Vendor_C(tDevice* device, bool USBchildDrive);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_Seagate_Model_Number_Vendor_D(tDevice* device, bool USBchildDrive);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_Seagate_Model_Number_Vendor_E(tDevice* device, bool USBchildDrive);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_Seagate_Model_Number_Vendor_SSD_PJ(tDevice* device, bool USBchildDrive);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_Seagate_Model_Number_Vendor_F(tDevice* device, bool USBchildDrive);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_Seagate_Model_Number_Vendor_G(tDevice* device, bool USBchildDrive);

    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_Seagate_Model_Number_Vendor_H(tDevice* device, bool USBchildDrive);

    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_Seagate_Vendor_K(tDevice* device);

    typedef enum eIronwolf_NAS_DriveEnum
    {
        NON_IRONWOLF_NAS_DRIVE,
        IRONWOLF_NAS_DRIVE,
        IRONWOLF_PRO_NAS_DRIVE,
    } eIronwolf_NAS_Drive;

    //-----------------------------------------------------------------------------
    //
    //  is_Ironwolf_NAS_Drive(tDevice *device, bool USBchildDrive)
    //
    //! \brief   Checks if the device is a Ironwolf or Ironwolf Pro NAS drive
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!   \param[in]  USBchildDrive - set to true to check USB child drive information. if set to false, this will
    //!   automatically also check the child drive info (this is really just used for recursion in the function)
    //!
    //  Exit:
    //!   \return 0 = Not a Ironwolf NAS Drive, 1 - a Ironwolf NAS Drive, 2 - a Ironwolf Pro NAS Drive
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API eIronwolf_NAS_Drive is_Ironwolf_NAS_Drive(tDevice* device, bool USBchildDrive);

    //-----------------------------------------------------------------------------
    //
    //  is_Firecuda_Drive(tDevice *device, bool USBchildDrive)
    //
    //! \brief   Checks if the device is a Firecuda drive
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!   \param[in]  USBchildDrive - set to true to check USB child drive information. if set to false, this will
    //!   automatically also check the child drive info (this is really just used for recursion in the function)
    //!
    //  Exit:
    //!   \return 1 = It is a Firecuda Drive, 0 - Not a Firecuda Drive
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_Firecuda_Drive(tDevice* device, bool USBchildDrive);

    typedef enum eSkyhawk_DriveEnum
    {
        NON_SKYHAWK_DRIVE,
        SKYHAWK_DRIVE,
        SKYHAWK_AI_DRIVE,
    } eSkyhawk_Drive;

    //-----------------------------------------------------------------------------
    //
    //  is_Skyhawk_Drive(tDevice *device, bool USBchildDrive)
    //
    //! \brief   Checks if the device is a Skyhawk or Skyhawk AI drive
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!   \param[in]  USBchildDrive - set to true to check USB child drive information. if set to false, this will
    //!   automatically also check the child drive info (this is really just used for recursion in the function)
    //!
    //  Exit:
    //!   \return 0 = Not a Skyhawk Drive, 1 - a Skyhawk Drive, 2 - a Skyhawk AI Drive
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API eSkyhawk_Drive is_Skyhawk_Drive(tDevice* device, bool USBchildDrive);

    //-----------------------------------------------------------------------------
    //
    //  is_Nytro_Drive(tDevice *device, bool USBchildDrive)
    //
    //! \brief   Checks if the device is a Firecuda drive
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!   \param[in]  USBchildDrive - set to true to check USB child drive information. if set to false, this will
    //!   automatically also check the child drive info (this is really just used for recursion in the function)
    //!
    //  Exit:
    //!   \return 1 = It is a Nytro Drive, 0 - Not a Nytro Drive
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_Nytro_Drive(tDevice* device, bool USBchildDrive);

    //-----------------------------------------------------------------------------
    //
    //  is_Exos_Drive(tDevice *device, bool USBchildDrive)
    //
    //! \brief   Checks if the device is a Firecuda drive
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!   \param[in]  USBchildDrive - set to true to check USB child drive information. if set to false, this will
    //!   automatically also check the child drive info (this is really just used for recursion in the function)
    //!
    //  Exit:
    //!   \return 1 = It is a Exos Drive, 0 - Not a Exos Drive
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_Exos_Drive(tDevice* device, bool USBchildDrive);

    //-----------------------------------------------------------------------------
    //
    //  is_Barracuda_Drive(tDevice *device, bool USBchildDrive)
    //
    //! \brief   Checks if the device is a Firecuda drive
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!   \param[in]  USBchildDrive - set to true to check USB child drive information. if set to false, this will
    //!   automatically also check the child drive info (this is really just used for recursion in the function)
    //!
    //  Exit:
    //!   \return 1 = It is a Barracuda Drive, 0 - Not a Barracuda Drive
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_Barracuda_Drive(tDevice* device, bool USBchildDrive);

    //-----------------------------------------------------------------------------
    //
    //  is_SSD( tDevice * device )
    //
    //! \brief   Checks if the device is an SSD or not. This just looks at the media type to see if it is MEDIA_SSD or
    //! MEDIA_NVM
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!
    //  Exit:
    //!   \return 1 = It is a SSD Drive, 0 - Not a SSD Drive
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_SSD(tDevice* device);

    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_SATA(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  is_Sector_Size_Emulation_Active( tDevice * device )
    //
    //! \brief   Checks if sector size emulation is active. This is only really useful on SAT interfaces/drivers. This
    //! will always be false when the isValid bool is set to false in thebridge info struct
    //!          This will likely only be true for some USB devices greater than 2.2TB, but SAT spec allows SAT devices
    //!          to emulate different sector sizes (although any emulation is vendor specific to the SATL)
    //
    //  Entry:
    //!   \param[in]  device - file descriptor
    //!
    //  Exit:
    //!   \return true = sector size emulation is active, false = sector size emulation is inactive, i.e. SATL reported
    //!   block size = device reported block size
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_Sector_Size_Emulation_Active(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  calculate_Checksum( uint8_t buf,  ) (OBSOLETE)
    //
    //! \brief  Calculates the ATA Spec. version of the checksum & returns the data
    //!         NOTE: 511th byte of the buffer will be changed.
    //!         This function has been replaced with a couple others in ata_helper_func.h since this is specific to ATA.
    //!
    //! A.14.7 Checksum
    //! The data structure checksum is the two?s complement of the sum of the first 511 bytes in the data structure.
    //! Each byte shall be added with eight-bit unsigned arithmetic and overflow shall be ignored. The sum of all 512
    //! bytes of the data structure shall be zero.
    //
    //  Entry:
    //!   \param[in, out] pBuf = uint8_t buffer to perform checksum on
    //!   \param[in] blockSize = uint32_t block size
    //  Exit:
    //!   \return int SUCCESS if passes !SUCCESS if fails for some reason.
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RW_SIZE(1, 2) OPENSEA_TRANSPORT_API eReturnValues calculate_Checksum(uint8_t* pBuf, uint32_t blockSize);

    //-----------------------------------------------------------------------------
    //
    //  get_Sector_Count_For_Read_Write(tDevice *device)
    //
    //! \brief  Gets the sectorCount based on the device interface. The value set is one that is most compatible across
    //! controllers/bridges and OSs
    //!         Will set 64K transfers for internal interfaces (SATA, SAS) and 32K for external (USB, IEEE1394)
    //
    //  Entry:
    //!   \param[in] device = pointer to the device struct.
    //
    //  Exit:
    //!   \return uint32_t value to use for a sector count
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API uint32_t get_Sector_Count_For_Read_Write(tDevice* device);

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
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API uint32_t get_Sector_Count_For_512B_Based_XFers(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  get_Sector_Count_For_4096B_Based_XFers(tDevice *device)
    //
    //! \brief  Gets the sectorCount based on the device interface for commands that are based on 4096B (4K) transfer
    //! blocks.
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
    M_NONNULL_PARAM_LIST(1)
    M_PARAM_RO(1) OPENSEA_TRANSPORT_API uint32_t get_Sector_Count_For_4096B_Based_XFers(tDevice* device);

    //-----------------------------------------------------------------------------
    //
    //  align_LBA()
    //
    //! \brief   Description:  This function takes an LBA, then aligns it to the beginning of a physical block. The
    //! return value is the LBA at the beginning of the physical block.
    //
    //  Entry:
    //!   \param[in] device = file descriptor
    //!   \param[in] LBA = the LBA that will be checked for alignment
    //!
    //  Exit:
    //!   \return The aligned LBA
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API uint64_t align_LBA(tDevice* device, uint64_t LBA);

    OPENSEA_TRANSPORT_API void print_Command_Time(uint64_t timeInNanoSeconds);

    OPENSEA_TRANSPORT_API void print_Time(uint64_t timeInNanoSeconds);

    OPENSEA_TRANSPORT_API void write_JSON_To_File(void* customData, char* message); // callback function

    typedef struct s_removeDuplicateDriveType
    {
        uint8_t csmi;
        uint8_t raid;
    } removeDuplicateDriveType;

    M_NONNULL_PARAM_LIST(1, 2)
    M_PARAM_RW(1)
    OPENSEA_TRANSPORT_API eReturnValues remove_Duplicate_Devices(tDevice*                 deviceList,
                                                                 volatile uint32_t*       numberOfDevices,
                                                                 removeDuplicateDriveType rmvDevFlag);

    M_NONNULL_PARAM_LIST(1, 3)
    M_PARAM_RW(1)
    OPENSEA_TRANSPORT_API eReturnValues remove_Device(tDevice*           deviceList,
                                                      uint32_t           driveToRemoveIdx,
                                                      volatile uint32_t* numberOfDevices);

    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_CSMI_Device(tDevice* device);

    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API bool is_Removable_Media(tDevice* device);

    M_NONNULL_PARAM_LIST(1) M_PARAM_RW(1) bool setup_Passthrough_Hacks_By_ID(tDevice* device);

#if defined(_DEBUG)
    // This function is more for debugging than anything else!
    void print_tDevice_Size(void);
#endif //_DEBUG

    //-----------------------------------------------------------------------------
    //
    //  print_Low_Level_Info(tDevice* device)
    //
    //! \brief   Description:  Printfs out useful low-level information from the device structure to the screen
    //
    //  Entry:
    //!   \param[in] device = file descriptor from an opened device
    //!
    //  Exit:
    //
    //-----------------------------------------------------------------------------
    M_NONNULL_PARAM_LIST(1) M_PARAM_RO(1) OPENSEA_TRANSPORT_API void print_Low_Level_Info(tDevice* device);

#if defined(__cplusplus)
} // extern "C"
#endif
