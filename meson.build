project('opensea-transport', 'c', license: 'MPL-2.0', version: '2.2.3', default_options : ['warning_level=2'])

c = meson.get_compiler('c')

#TODO: Add -Wcast-align=strict and fix these issues to help ensure better portability
#NOTE: -Wsign-conversion can be useful while debugging, but there are numerous places this shows up
#      and it is not useful, so only add it while debugging.
warning_flags = [
#	'-Wcast-align=strict',
#	'-Wsign-conversion',
	'-Wshadow=compatible-local',
	'-Wvla',
	'-Wfloat-equal',
	'-Wnull-dereference',
	'-Wunused-const-variable',
	'-Wduplicated-cond',
	'-Wjump-misses-init',
	'-Wstringop-overflow',
	'-Wlogical-op',
	'-Wshift-overflow=2',
	'-Wdouble-promotion',
	'-Wformat-security',
  '-Wold-style-definition',
  '-Wstrict-prototypes',
  '-Wmissing-declarations',
  '-Wmissing-prototypes'
]

add_project_arguments(c.get_supported_arguments(warning_flags), language : 'c')

if get_option('debug')
  add_project_arguments('-D_DEBUG', language : 'c')
endif

global_cpp_args = []

#if GCC less than 5, need to set -std=gnu99 at minimum. gnu11 became the default in 5, 17 default in 7 or 8.
#TODO: May be able to move to c11/gnu11 instead, but will need to do a lot of testing first
#skipping sunos since this was a compatibility issue that was reported earlier. May be able to find a better way to handle this in the future.
if not (target_machine.system() == 'sunos') and c.get_id().contains('gcc')
  verStr = c.version().split('.')
  if verStr.get(0).to_int() < 5
      global_cpp_args += ['-std=gnu99']
  endif
endif

src_files = ['src/asmedia_nvme_helper.c', 'src/ata_cmds.c', 'src/ata_helper.c', 'src/ata_legacy_cmds.c', 'src/cmds.c', 'src/common_public.c', 'src/csmi_helper.c', 'src/csmi_legacy_pt_cdb_helper.c', 'src/cypress_legacy_helper.c', 'src/intel_rst_helper.c', 'src/jmicron_nvme_helper.c', 'src/nec_legacy_helper.c', 'src/nvme_cmds.c', 'src/nvme_helper.c', 'src/of_nvme_helper.c', 'src/prolific_legacy_helper.c', 'src/psp_legacy_helper.c', 'src/raid_scan_helper.c', 'src/sata_helper_func.c', 'src/sat_helper.c', 'src/scsi_cmds.c', 'src/scsi_helper.c', 'src/sntl_helper.c', 'src/ti_legacy_helper.c', 'src/usb_hacks.c']

os_deps = []

if target_machine.system() == 'linux'
  src_files += ['src/sg_helper.c']
elif target_machine.system() == 'freebsd'
  src_files += ['src/cam_helper.c']
  cam_dep = c.find_library('cam')
  os_deps += [cam_dep]
elif target_machine.system() == 'sunos'
  src_files += ['src/uscsi_helper.c']
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
