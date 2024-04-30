# SPDX-License-Identifier: MPL-2.0
project('opensea-transport', 'c', license: 'MPL-2.0', version: '3.0.1', default_options : ['warning_level=2'])

c = meson.get_compiler('c')

warning_flags = [ ]
linker_flags = [ ] #additional linker flags to add per-compiler for hardening.

if c.get_id().contains('gcc') or c.get_id().contains('clang')
  #TODO: Add -Wcast-align=strict and fix these issues to help ensure better portability
  #NOTE: -Wsign-conversion can be useful while debugging, but there are numerous places this shows up
  #      and it is not useful, so only add it while debugging.
  #NOTE: 4/4/2024 - adding flags from https://best.openssf.org/Compiler-Hardening-Guides/Compiler-Options-Hardening-Guide-for-C-and-C++
  warning_flags = [
  #	'-Wcast-align=strict',
	'-Wshadow=compatible-local',
	'-Wvla',
	'-Wfloat-equal',
	'-Wnull-dereference',
	'-Wunused-const-variable',
	'-Wduplicated-cond',
	'-Wjump-misses-init',
	'-Wstringop-overflow',
	'-Wlogical-op',
  '-Wshift-overflow',
  '-Wshift-overflow=1',
	'-Wshift-overflow=2',
	'-Wdouble-promotion',
	'-Wformat-security',
  '-Wold-style-definition',
  '-Wstrict-prototypes',
  '-Wmissing-declarations',
  '-Wmissing-prototypes',
  '-Wchar-subscripts',
  '-Wundef',
  '-Wformat',
  '-Wformat=2',
  #'-Wconversion',
  '-Wsign-conversion',
  '-Wint-conversion',#-Warith-conversion
  '-Wenum-conversion',
  '-Wfloat-conversion',
  '-Wimplicit-fallthrough',
  '-U_FORTIFY_SOURCE',
  '-D_FORTIFY_SOURCE=3',
  '-D_GLIBCXX_ASSERTIONS',
  '-fstrict-flex-arrays=1', #NOTE: Using level 1 since Windows often uses [1] at the end of it's structures. opensea-*libs has used this in a few places too.
  '-fstack-protector-strong',
  '-fno-delete-null-pointer-checks',
  '-fno-strict-overflow',
  '-fno-strict-aliasing',
  '-ftrivial-auto-var-init=zero',
  '-Wtrampolines', #GCC only at this time
  '-Werror=implicit',
  '-Werror=incompatible-pointer-types',
  '-Werror=int-conversion'
  ]

  if c.get_id().contains('gcc') and target_machine.system() == 'windows'
    #According to the link below, this is not needed in Windows...it also causes a bug in some versions of GCC for Windows.
    #https://gcc.gnu.org/bugzilla/show_bug.cgi?id=90458
    #NOTE: While this appears to be fixed for versions 11 and 12, the current CI is failing on this with
    #      version 12.2. If we want to or need to enable this, it should be done based on which versions we
    #      know have been patched for this. -TJE
  else
    warning_flags += '-fstack-clash-protection'
  endif

  if target_machine.cpu_family() == 'ppc64'
    #power pc builds generate a lot of warnings/notes about ABI changes since GCC-5
    #this flag is disabling them because this is way too noisy.
    warning_flags += ['-Wno-psabi']
  elif target_machine.cpu_family() == 'x86_64'
    warning_flags += ['-fcf-protection=full'] #this may be linux only at this time.
  elif target_machine.cpu_family() == 'aarch64'
    warning_flags += ['-mbranch-protection=standard']
  endif

  linker_flags += [
    '-Wl,-z,nodlopen',
    '-Wl,-z,noexecstack',
    '-Wl,-z,relro',
    '-Wl,-z,now'
  ]
  if target_machine.cpu_family() == 'ppc64'
    #power pc builds generate a lot of warnings/notes about ABI changes since GCC-5
    #this flag is disabling them because this is way too noisy.
    warning_flags += ['-Wno-psabi']
  endif
elif c.get_id().contains('msvc')
  #See here for enabling/disabling msvc warnings:
  #https://learn.microsoft.com/en-us/cpp/build/reference/compiler-option-warning-level?view=msvc-170
  #warnings off by default: https://learn.microsoft.com/en-us/cpp/preprocessor/compiler-warnings-that-are-off-by-default?view=msvc-170
  warning_flags = [
    #Turn off the following warnings. If using /wall in Windows, many of these show all over the Windows API
    #This is likely not an issue with meson, but matching VS project files for now
    '/wd4214', # nonstandard extension used : bit field types other than int
    '/wd4201', # nonstandard extension used : nameless struct/union
    '/wd4668', # 'symbol' is not defined as a preprocessor macro, replacing with '0' for 'directives'. While like -Wundef, this creates too many warnings in system headers to use
    '/wd4820', # 'bytes' bytes padding added after construct 'member_name'
    '/wd4710', # 'function' : function not inlined
    #'/wd4255', # 'function' : no function prototype given: converting '()' to '(void)' #NOTE: Only needed for /Wall, otherwise enabling can be good-TJE
    '/wd5045', # Compiler will insert Spectre mitigation for memory load if /Qspectre switch specified
    '/wd4711', # function 'function' selected for inline expansion
    '/wd4324', # 'struct_name' : structure was padded due to __declspec(align())
    '/wd4221', # nonstandard extension used : 'identifier' : cannot be initialized using address of automatic variable
    '/wd4204', # nonstandard extension used : non-constant aggregate initializer
    '/wd4061', # enumerator 'identifier' in switch of enum 'enumeration' is not explicitly handled by a case label
    '/wd5105', # macro expansion producing 'defined' has undefined behavior
    '/wd4746', # volatile access of '<expression>' is subject to /volatile:[iso|ms] setting; consider using __iso_volatile_load/store intrinsic functions.
    #Turn on the following warnings to make the output more useful or like GCC/clang
    '/w14255', # 'function' : no function prototype given: converting '()' to '(void)'
    '/w14062', # enumerator 'identifier' in switch of enum 'enumeration' is not handled
    '/w14101', # 'identifier' : unreferenced local variable
    '/w14189', # 'identifier' : local variable is initialized but not referenced
    '/w15031', # #pragma warning(pop): likely mismatch, popping warning state pushed in different file
    '/w15032', # detected #pragma warning(push) with no corresponding #pragma warning(pop)
    '/w15262', # implicit fall-through occurs here; are you missing a break statement? Use [[fallthrough]] when a break statement is intentionally omitted between cases
    #Treat the following as errors
    '/we4837', # trigraph detected: '??character' replaced by 'character'
    '/we4628', # digraphs not supported with -Ze. Character sequence 'digraph' not interpreted as alternate token for 'char'
    '/we4289', # nonstandard extension used : 'var' : loop control variable declared in the for-loop is used outside the for-loop scope
    '/std:c17', #NOTE: It would be better to specify for the project above, but currently still supporting MSVC and GCC compilers that may not be C11 compatible.-TJE
    '/GS', #security cookie for stack protection
    '/sdl', #adds recommended security development lifecycle checks
    '/Qspectre',
    '/guard:cf', #control flow guard
    '/d2guard4', #control flow guard
  ]

  linker_flags += [
    '/guard:cf', #control flow guard
    '/SafeSEH', #on by default in x64 so it is unrecognized otherwise.
    '/NXCOMPAT', #data execution prevention
    '/dynamicbase', #address space randomization
  ]
  #TODO: check compiler version to handle warnings that were off by default in earlier versions
  #ex: C4431 (level 4)	missing type specifier - int assumed. Note: C no longer supports default-int
  #    This was off by default in compilers before VS2012.
elif c.get_id().contains('xlc')
  #This section is for IBM's xlc compiler and warning options it may need.
  #NOTE: xlcclang should be handled above
  #See following links: 
  #https://www.ibm.com/docs/en/xl-c-and-cpp-aix/16.1?topic=reference-supported-xl-compiler-options-by-different-invocations
  #https://www.ibm.com/docs/en/xl-c-and-cpp-aix/16.1?topic=end-mapping-legacy-xl-compiler-options-gcc-options
  #https://www.ibm.com/docs/en/xl-c-and-cpp-aix/16.1?topic=reference-individual-xl-compiler-option-descriptions
  warning_flags = []
endif

add_project_arguments(c.get_supported_arguments(warning_flags), language : 'c')
add_project_link_arguments(c.get_supported_link_arguments(linker_flags), language : 'c')

if get_option('debug')
  add_project_arguments('-D_DEBUG', language : 'c')
endif

global_cpp_args = []

#if GCC less than 5, need to set -std=gnu99 at minimum. gnu11 became the default in 5, 17 default in 7 or 8.
#TODO: May be able to move to c11/gnu11 instead, but will need to do a lot of testing first
#skipping sunos since this was a compatibility issue that was reported earlier. May be able to find a better way to handle this in the future.
if not (target_machine.system() == 'sunos') and c.get_id().contains('gcc')
  if c.version().version_compare('<5.0')
    if c.has_argument('-std=gnu99')
      #Add this argument to the list since C99 is a minimum required C compiler standard
      add_project_arguments('-std=gnu99', language: 'c',)
    else
      error('C99/GNU99 standard is required but was not able to be set!')
    endif
  endif
endif

src_files = ['src/asmedia_nvme_helper.c', 
             'src/ata_cmds.c', 
             'src/ata_helper.c', 
             'src/ata_legacy_cmds.c', 
             'src/ciss_helper.c',
             'src/cmds.c', 
             'src/common_public.c', 
             'src/csmi_helper.c', 
             'src/csmi_legacy_pt_cdb_helper.c', 
             'src/cypress_legacy_helper.c', 
             'src/intel_rst_helper.c', 
             'src/jmicron_nvme_helper.c', 
             'src/nec_legacy_helper.c', 
             'src/nvme_cmds.c', 
             'src/nvme_helper.c', 
             'src/of_nvme_helper.c', 
             'src/prolific_legacy_helper.c', 
             'src/psp_legacy_helper.c', 
             'src/raid_scan_helper.c', 
             'src/sata_helper_func.c', 
             'src/sat_helper.c', 
             'src/scsi_cmds.c', 
             'src/scsi_helper.c', 
             'src/sntl_helper.c', 
             'src/ti_legacy_helper.c', 
             'src/usb_hacks.c']

os_deps = []

if target_machine.system() == 'linux'
  src_files += ['src/sg_helper.c']
  add_project_arguments('-DENABLE_CISS', '-DENABLE_CSMI', language : 'c')
elif target_machine.system() == 'freebsd'
  src_files += ['src/cam_helper.c']
  cam_dep = c.find_library('cam')
  os_deps += [cam_dep]
elif target_machine.system() == 'sunos'
  src_files += ['src/uscsi_helper.c']
elif target_machine.system() == 'aix'
  src_files += ['src/aix_helper.c']
  odm_dep = c.find_library('odm')
  os_deps += [odm_dep]
  cfg_dep = c.find_library('cfg')
  os_deps += [cfg_dep]
elif target_machine.system() == 'windows'
  src_files += ['src/win_helper.c']
  if c.get_define('__MINGW32__') != ''
  	#BR note: -D_WIN32_WINNT=0x0601 fixes unknown Windows version in winioctl.h and errors such as unknown type name ‘PDEVICE_LB_PROVISIONING_DESCRIPTOR’
	add_project_arguments('-D_WIN32_WINNT=0x0601', language : 'c')
  	cfgmgr32 = c.find_library('cfgmgr32')
  	os_deps += [cfgmgr32]
  endif
  add_project_arguments('-DENABLE_INTEL_RST', '-DENABLE_OFNVME', '-DENABLE_CSMI', language : 'c')
  global_cpp_args += ['-D_CRT_NONSTDC_NO_DEPRECATE', '-D_CRT_SECURE_NO_WARNINGS']
  global_cpp_args += ['-DSTATIC_OPENSEA_TRANSPORT', '-D_UNICODE', '-DUNICODE']
endif # TODO UEFI and vmware

opensea_common = subproject('opensea-common')
opensea_common_dep = opensea_common.get_variable('opensea_common_dep')

incdir = include_directories('include', 'include/vendor')

opensea_transport_lib = static_library('opensea-transport', src_files, c_args : global_cpp_args, dependencies : [opensea_common_dep, os_deps], include_directories : incdir)
opensea_transport_dep = declare_dependency(link_with : opensea_transport_lib, compile_args : global_cpp_args, dependencies : os_deps, include_directories : incdir)
