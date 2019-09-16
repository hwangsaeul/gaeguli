gaeguli_install_header_subdir = join_paths(gaeguli_api_name, 'gaeguli')

configure_file(output: 'config.h', configuration: cdata)

source_h = [
  'types.h',
  'pipeline.h',
  'fifo-transmit.h',
]

source_c = [
  'types.c',
  'pipeline.c',
  'fifo-transmit.c',
]

install_headers(source_h, subdir: gaeguli_install_header_subdir)

gaeguli_c_args = [
  '-DG_LOG_DOMAIN="GAEGULI"',
  '-DGAEGULI_COMPILATION',
]

gaeguli_enums = gnome.mkenums(
  'enumtypes.h',
  h_template: 'enumtypes.h.template',
  c_template: 'enumtypes.c.template',
  sources: source_h,
  install_dir: join_paths(get_option('includedir'), gaeguli_install_header_subdir),
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
  dependencies: [ gobject_dep, gio_dep, gst_dep, libsrt_dep ],
  c_args: gaeguli_c_args,
  link_args: common_ldflags,
  install: true
)

libgaeguli_dep = declare_dependency(link_with: libgaeguli,
  include_directories: [ gaeguli_incs ],
  dependencies: [ gobject_dep, gio_dep, gst_dep ],
  sources: [ gaeguli_enums_h ],
)


