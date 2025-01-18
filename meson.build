# SPDX-License-Identifier: MPL-2.0
project('opensea-transport', 'c', license: 'MPL-2.0', version: '8.0.1', default_options : ['warning_level=2'])

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
  '-Wunused-parameter',
  '-Wunused-value',
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
  '-Wint-conversion',#-Warith-conversion
  '-Wenum-conversion',
  '-Wfloat-conversion',
  '-Wint-to-pointer-cast',
  '-Wimplicit-fallthrough',
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
  '-Wincompatible-pointer-types-discards-qualifiers',
  '-Werror=int-conversion',
  '-Werror=implicit-int',
  '-Woverlength-strings',
  '-Wnewline-eof',
  '-Wno-c23-extensions', #We do not want this warning since we are already checking for when C23 extensions are available before we use them. If not, we use a compiler specific definition, or make it an empty definition.
  '-Wparentheses',
  '-Wextra-semi',
  '-Wcast-qual',
  '-Werror=sometimes-uninitialized',
  '-Wuninitialized',
  '-Wunevaluated-expression',
  '-Wunsequenced',
  '-Wvarargs',
  '-Wwrite-strings',
  '-Wrestrict',
  '-Wstringop-truncation',
  '-Werror=trigraphs',
  '-Wunreachable-code',
  '-Wcomment',
  '-Wsequence-point',
  '-Wreturn-type',
  '-Wpointer-bool-conversion',
  '-fvisibility=hidden', #to work similarly to Window's DLL import/export
  ]

  if c.get_id().contains('gcc') and c.version().version_compare('>=10.0')
    #only enable the sign conversion warning on versions 10 and up because it is way too noisy on earlier GCC versions than it is useful-TJE
    warning_flags += '-Wsign-conversion'
  endif

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

  fortifytest = ''' #include <stdio.h>
                      int main() {
                          return 0;
                      }
                  '''
  fortifyresult = c.compiles(fortifytest, name : '_FORTIFY_SOURCE override', args : ['-U_FORTIFY_SOURCE', '-D_FORTIFY_SOURCE=5', '-Werror'])
  if fortifyresult == true
    warning_flags += ['-U_FORTIFY_SOURCE', '-D_FORTIFY_SOURCE=3']
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
    '/wd5045', # Compiler will insert Spectre mitigation for memory load if /Qspectre switch specified
    '/wd4711', # function 'function' selected for inline expansion
    '/wd4324', # 'struct_name' : structure was padded due to __declspec(align())
    '/wd4221', # nonstandard extension used : 'identifier' : cannot be initialized using address of automatic variable
    '/wd4204', # nonstandard extension used : non-constant aggregate initializer
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
    '/w14255', # 'function' : no function prototype given: converting '()' to '(void)' #NOTE: Only needed for /Wall, otherwise enabling can be good-TJE
    '/w14242', # identifier conversion from type 1 to type 2, possible loss of data (matches -wconversion above)
    '/w14254', # operator conversion from type 1 to type 2, possible loss of data (matches -wconversion above)
    '/w14287', # operator: unsigned/negative constant mismatch (matches -wconversion above)
    '/w14296', # operator: expression is always false
    '/w14365', # action: conversion from type 1 to type 2, signed/unsigned mismatch (matches -wconversion above)
    '/w14388', # implicit conversion warning during a comparison (matches -wconversion above)
    '/w14545', # expression before comma evaluates to a function which is missing an argument list
    '/w14546', # function call before comma missing argument list
    '/w14547', # 'operator' : operator before comma has no effect; expected operator with side-effect
    '/w14548', # expression before comma has no effect; expected expression with side-effect
    '/w14549', # 'operator1': operator before comma has no effect; did you intend 'operator2'?
    '/w14574', # 'identifier' is defined to be '0': did you mean to use '#if identifier'?
    '/w14605', # 	'/Dmacro' specified on current command line, but was not specified when precompiled header was built
    '/w14555', # expression has no effect; expected expression with side-effect
    '/w14774', # 'string' : format string expected in argument number is not a string literal
    '/w14777', # 'function' : format string 'string' requires an argument of type 'type1', but variadic argument number has type 'type2'
    '/w14826', # Conversion from 'type1' to 'type2' is sign-extended. This may cause unexpected runtime behavior (more -wconversion)
    '/w15219', # implicit conversion from 'type-1' to 'type-2', possible loss of data (-wconversion)
    '/w15240', # 'attribute-name': attribute is ignored in this syntactic position 
    '/w15245', # 'function': unreferenced function with internal linkage has been removed
    '/w14555', # expression has no effect; expected expression with side-effect
    '/w15264', # 'variable-name': 'const' variable is not used
    '/w24302', # 'conversion': truncation from 'type1' to 'type2'
    '/w14311', # 'variable': pointer truncation from 'type' to 'type'
    '/w14312', # 'operation': conversion from 'type1' to 'type2' of greater size
    '/w14319', # 'operator': zero extending 'type1' to 'type2' of greater size
    #Treat the following as errors
    '/we4431', # missing type specifier - int assumed. Note: C no longer supports default-int
    '/we4905', # wide string literal cast to 'LPSTR'
    '/we4906', # string literal cast to 'LPWSTR'
    '/we4837', # trigraph detected: '??character' replaced by 'character'
    '/we4628', # digraphs not supported with -Ze. Character sequence 'digraph' not interpreted as alternate token for 'char'
    '/we4289', # nonstandard extension used : 'var' : loop control variable declared in the for-loop is used outside the for-loop scope
    '/we4464', # relative include path contains '..'
    '/GS', #security cookie for stack protection
    '/sdl', #adds recommended security development lifecycle checks
    '/Qspectre',
    '/guard:cf', #control flow guard
    '/d2guard4', #control flow guard
  ]

  if target_machine.cpu_family() == 'x86_64' or target_machine.cpu_family() == 'x86'
    # https://learn.microsoft.com/en-us/cpp/build/reference/qintel-jcc-erratum?view=msvc-170
    warning_flags += '/QIntel-jcc-erratum'
  endif

  if c.has_argument('/std:c17')
    c_std = 'c17'
  endif

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

if not (target_machine.system() == 'sunos') and c.get_id().contains('gcc')
  if c.version().version_compare('<5.0')
  #4.7.4+ has C11 support, but c89 is the default standard so we need to change it.
    if c.has_argument('-std=gnu11')
        c_std = 'gnu11'
        if meson.version().version_compare('<1.0.0')
          add_project_arguments('-std=gnu11', language : 'c')
        endif
      elif c.has_argument('-std=gnu99')
        #Add this argument to the list since C99 is a minimum required C compiler standard
        c_std = 'gnu99'
        if meson.version().version_compare('<1.0.0')
          add_project_arguments('-std=gnu99', language : 'c')
        endif
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
             'src/realtek_nvme_helper.c', 
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

# Handle various options related to NVMe passthrough support, CISS support, CSMI support, etc. All of these SHOULD be on by default but can be disabled.
nvmesupport = get_option('nvmept')
if nvmesupport.disabled()
  add_project_arguments('-DDISABLE_NVME_PASSTHROUGH', language : 'c')
endif
csmisupport = get_option('csmiraid')
if csmisupport.enabled()
  add_project_arguments('-DENABLE_CSMI', language : 'c')
endif
cisssupport = get_option('cissraid')
irstsupport = get_option('irstpt')
ofnvmesupport = get_option('ofnvme')

os_deps = []

if target_machine.system() == 'linux'
  src_files += ['src/sg_helper.c']
  if cisssupport.enabled()
    add_project_arguments('-DENABLE_CISS', language : 'c')
  endif
  if nvmesupport.enabled()
    if c.check_header('linux/nvme_ioctl.h')
      global_cpp_args += ['-DSEA_NVME_IOCTL_H']
    elif c.check_header('linux/nvme.h')
      global_cpp_args += ['-DSEA_NVME_H']
    elif c.check_header('uapi/nvme.h')
      global_cpp_args += ['-DSEA_UAPI_NVME_H']
    else
      add_project_arguments('-DDISABLE_NVME_PASSTHROUGH', language : 'c')
      message('Auto-disabling NVMe support as no ioctl header can be found')
    endif
  endif
elif target_machine.system() == 'freebsd'
  src_files += ['src/cam_helper.c']
  cam_dep = c.find_library('cam')
  os_deps += [cam_dep]
  #TODO: Need to do some testing on FreeBSD before we allow this
  # if cisssupport.enabled()
  #   add_project_arguments('-DENABLE_CISS', language : 'c')
  # endif
  if not c.check_header('dev/nvme/nvme.h')
    add_project_arguments('-DDISABLE_NVME_PASSTHROUGH', language : 'c')
    message('Auto-disabling NVMe support as no ioctl header can be found')
  endif
elif target_machine.system() == 'sunos'
  src_files += ['src/uscsi_helper.c']
  add_project_arguments('-DDISABLE_NVME_PASSTHROUGH', language : 'c')
  message('Auto-disabling NVMe support as no ioctl header can be found')
  #TODO: Need to do some testing on Illumos before we allow this
  # if cisssupport.enabled()
  #   add_project_arguments('-DENABLE_CISS', language : 'c')
  # endif
elif target_machine.system() == 'aix'
  src_files += ['src/aix_helper.c']
  odm_dep = c.find_library('odm')
  os_deps += [odm_dep]
  cfg_dep = c.find_library('cfg')
  os_deps += [cfg_dep]
  if not c.check_header('sys/nvme.h')
    add_project_arguments('-DDISABLE_NVME_PASSTHROUGH', language : 'c')
    message('Auto-disabling NVMe support as no ioctl header can be found')
  endif
elif target_machine.system() == 'windows'
  src_files += ['src/win_helper.c']
  if c.get_define('__MINGW32__') != ''
    #BR note: -D_WIN32_WINNT=0x0601 fixes unknown Windows version in winioctl.h and errors such as unknown type name ‘PDEVICE_LB_PROVISIONING_DESCRIPTOR’
    add_project_arguments('-D_WIN32_WINNT=0x0601', language : 'c')
    cfgmgr32 = c.find_library('cfgmgr32')
    os_deps += [cfgmgr32]
    #TODO: Need to figure out a better way to detect when it is possible to include NVMe passthrough support with MinGW
    add_project_arguments('-DDISABLE_NVME_PASSTHROUGH', language : 'c')
    message('Auto-disabling NVMe support as no ioctl header can be found')
  endif
  if irstsupport.enabled()
    add_project_arguments('-DENABLE_INTEL_RST', language : 'c')
  endif
  if ofnvmesupport.enabled()
    add_project_arguments('-DENABLE_OFNVME', language : 'c')
  endif
  global_cpp_args += ['-D_CRT_NONSTDC_NO_DEPRECATE', '-D_CRT_SECURE_NO_WARNINGS']
  global_cpp_args += ['-DSTATIC_OPENSEA_TRANSPORT', '-D_UNICODE', '-DUNICODE']
endif # TODO UEFI and vmware

opensea_common = subproject('opensea-common')
opensea_common_dep = opensea_common.get_variable('opensea_common_dep')

incdir = include_directories('include', 'include/vendor')

opensea_transport_lib = static_library('opensea-transport', src_files, c_args : global_cpp_args, dependencies : [opensea_common_dep, os_deps], include_directories : incdir)
opensea_transport_dep = declare_dependency(link_with : opensea_transport_lib, compile_args : global_cpp_args, dependencies : os_deps, include_directories : incdir)
