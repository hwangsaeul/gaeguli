gaeguli_install_header_subdir = join_paths(gaeguli_api_name, 'gaeguli')

configure_file(output: 'config.h', configuration: cdata)

source_h = [
  'types.h',
  'edge.h',
  'pipeline.h',
  'fifo-transmit.h',
]

source_c = [
  'edge.c',
  'pipeline.c',
  'fifo-transmit.c',
]

install_headers(source_h, subdir: gaeguli_install_header_subdir)

gaeguli_c_args = [
  '-DG_LOG_DOMAIN="GAEGULI"',
  '-DGAEGULI_COMPILATION',
]

gaeguli_enums = gnome.mkenums_simple(
  'enumtypes',
  header_prefix: '#include <gaeguli/types.h>',
  sources: source_h,
  decorator: 'GAEGULI_API_EXPORT',
  install_dir: gaeguli_install_header_subdir,
  install_header: true,
)

gaeguli_enums_h = gaeguli_enums[1]
gaeguli_enums_c = gaeguli_enums[0]

libgaeguli = library(
  'gaeguli-@0@'.format(apiversion),
  gaeguli_enums, source_c,
  version: libversion,
  soversion: soversion,
  include_directories: gaeguli_incs,
  dependencies: [ gobject_dep, gio_dep, libsrt_dep ],
  c_args: gaeguli_c_args,
  link_args: common_ldflags,
  install: true
)

libgaeguli_dep = declare_dependency(link_with: libgaeguli,
  include_directories: [ gaeguli_incs ],
  dependencies: [ gobject_dep, gio_dep ],
  sources: [ gaeguli_enums_h ],
)


