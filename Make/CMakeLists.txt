cmake_minimum_required(VERSION "3.12")

if (MSVC)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/VS.2017/opensea-transport/x64/LIB/${CMAKE_BUILD_TYPE}")
	set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/VS.2017/opensea-transport/x64/LIB/${CMAKE_BUILD_TYPE}")
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/VS.2017/opensea-transport/x64/LIB/${CMAKE_BUILD_TYPE}")
	elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
	set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/VS.2017/opensea-transport/Win32/LIB/${CMAKE_BUILD_TYPE}")
	set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/VS.2017/opensea-transport/Win32/LIB/${CMAKE_BUILD_TYPE}")
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/VS.2017/opensea-transport/Win32/LIB/${CMAKE_BUILD_TYPE}")
	
    endif()
 
    add_definitions(-D WIN_API_TARGET_VERSION=100162990L)
    add_definitions(-D ENABLE_CSMI)
    add_definitions(-D ENABLE_OFNVME)
	add_definitions(-D ENABLE_INTEL_RST)
    add_definitions(-D_UNICODE)
    add_definitions(-D UNICODE)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS) 
	add_definitions(-D STATIC_OPENSEA_TRANSPORT)

elseif (UNIX)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/gcc/LIB")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/gcc/LIB")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/gcc/LIB")
endif()

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../include")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../../opensea-common/include")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../include/vendor")

if (MSVC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4214 /wd4201 /wd4668 /wd4820 /wd4710 /wd4255 /wd5045 /wd4711 /wd4324 /wd4221 /wd4204 /JMC /sdl /EHsc")
    SET(GCC_COVERAGE_LINK_FLAGS    "-Wno-dev")

    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        link_directories("${CMAKE_CURRENT_SOURCE_DIR}/../../opensea-common/Make/VS.2017/opensea-common/x64/LIB/${CMAKE_BUILD_TYPE}")
		set(CMAKE_LIBRARY_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../opensea-common/Make/VS.2017/opensea-common/x64/LIB/${CMAKE_BUILD_TYPE}")
    elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
        link_directories("${CMAKE_CURRENT_SOURCE_DIR}/../../opensea-common/Make/VS.2017/opensea-common/Win32/LIB/${CMAKE_BUILD_TYPE}")
    endif()
elseif (UNIX)
    add_definitions(-DSEA_NVME_IOCTL_H)
    set (CMAKE_C_FLAGS "-Wall -fPIC -I. -std=gnu99 ${CMAKE_C_FLAGS}") 
    link_directories("${CMAKE_CURRENT_SOURCE_DIR}/../../opensea-common/Make/gcc/LIB")
endif()

add_library(opensea-transportLib STATIC ${SOURCES} ${ADD_SRCS})

add_library(opensea-transport-DLL SHARED ${SOURCES} ${ADD_SRCS})

if (UNIX)
    target_link_libraries(opensea-transport-DLL opensea-common.a)
elseif (MSVC)
	target_link_libraries(opensea-transportLib PRIVATE Cfgmgr32.lib)
    target_link_libraries(opensea-transport-DLL opensea-common.lib)
endif()


