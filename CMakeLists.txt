cmake_minimum_required(VERSION "3.12")

project(opensea-transport)
set(CMAKE_CONFIGURATION_TYPES "Debug" "Release" "Static-Debug" "Static-Release")

if (CMAKE_GENERATOR_PLATFORM MATCHES "ARM")
	set(CMAKE_C_FLAGS_DEBUG "/Zi /Wall /diagnostics:caret /JMC /Od /Od /EHsc /RTC1 /MDd")
	set(CMAKE_C_FLAGS_STATIC-DEBUG "/Zi /Wall /diagnostics:caret /JMC /Od /Od /EHsc /RTC1 /MTd")
elseif (CMAKE_GENERATOR_PLATFORM MATCHES "ARM64")
	set(CMAKE_C_FLAGS_DEBUG "/Zi /Wall /diagnostics:caret /JMC /Od /Od /EHsc /RTC1 /MDd")
	set(CMAKE_C_FLAGS_STATIC-DEBUG "/Zi /Wall /diagnostics:caret /JMC /Od /Od /EHsc /RTC1 /MTd")
else()
	set(CMAKE_C_FLAGS_DEBUG "/ZI /JMC /Wall /diagnostics:caret /Oy- /Od /Od /EHsc /RTC1 /MDd")
	set(CMAKE_C_FLAGS_STATIC-DEBUG "/ZI /JMC /Wall /diagnostics:caret /Od /Od /EHsc /RTC1 /MTd")
endif()

set(CMAKE_C_FLAGS_RELEASE "/LTCG /Wall /diagnostics:caret /GL /Gy /Oi /EHsc /O2 /Zi ")
set(CMAKE_C_FLAGS_STATIC-RELEASE "/LTCG /Wall /diagnostics:caret /GL  /Gy /Zi /Oi /EHsc /MT")

file (GLOB SOURCES src/asmedia_nvme_helper.c
				src/ata_cmds.c
				src/ata_helper.c
				src/ata_legacy_cmds.c
				src/cmds.c
				src/common_public.c
				src/csmi_helper.c
				src/cypress_legacy_helper.c
				src/csmi_legacy_pt_cdb_helper.c
				src/intel_rst_helper.c
				src/jmicron_nvme_helper.c
				src/nec_legacy_helper.c
				src/nvme_cmds.c
				src/nvme_helper.c
				src/of_nvme_helper.c
				src/prolific_legacy_helper.c
				src/psp_legacy_helper.c
				src/raid_scan_helper.c
				src/sat_helper.c
				src/sata_helper_func.c
				src/scsi_cmds.c
				src/scsi_helper.c
				src/sntl_helper.c
				src/ti_legacy_helper.c
				src/usb_hacks.c
				include/vendor/seagate_ata_types.h 
				include/asmedia_nvme_helper.h 
				include/ata_helper.h 
				include/ata_helper_func.h 
				include/cmds.h 
				include/common_public.h 
				include/csmi_helper.h 
				include/csmi_helper_func.h 
				include/csmisas.h 
				include/cypress_legacy_helper.h 
				include/jmicron_nvme_helper.h 
				include/nec_legacy_helper.h 
				include/nvme_helper.h 
				include/nvme_helper_func.h 
				include/of_nvme_helper.h 
				include/of_nvme_helper_func.h 
				include/of_nvmeIoctl.h 
				include/platform_helper.h 
				include/prolific_legacy_helper.h 
				include/psp_legacy_helper.h 
				include/sat_helper.h 
				include/sat_helper_func.h 
				include/scsi_helper.h 
				include/scsi_helper_func.h 
				include/sntl_helper.h 
				include/ti_legacy_helper.h 
				include/usb_hacks.h 
				include/version.h)

add_definitions(-D_UNICODE)

set(CMAKE_SYSTEM_VERSION "${10.0.16299.0}")
set(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION "${10.0.16299.0}")
set(CMAKE_SUPPRESS_REGENERATION true)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(MSVC)
    file(GLOB ADD_SRCS src/win_helper.c include/win_helper.h)
elseif(UNIX)
    file(GLOB ADD_SRCS src/sg_helper.c)
endif()

				
add_subdirectory(Make)


