pkg = import('pkgconfig')

gaeguli_install_header_subdir = join_paths(gaeguli_api_name, 'gaeguli')

configure_file(output: 'config.h', configuration: cdata)

source_h = [
  'gaeguli.h',
  'types.h',
  'pipeline.h',
  'streamadaptor.h',
]

source_c = [
  'types.c',
  'pipeline.c',
  'streamadaptor.c',
  'adaptors/nulladaptor.c',
  'adaptors/bandwidthadaptor.c',
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

if meson.version().version_compare('>= 0.48.2')

  pkg.generate(libgaeguli,
    libraries : [ gst_dep, libsrt_dep ],
    name : meson.project_name(),
    description : 'A SRT Streaming library',
    filebase : gaeguli_api_name,
    subdirs: gaeguli_api_name,
    variables: 'exec_prefix=${prefix}'
  )

else

  pkg.generate(
    libraries : libgaeguli,
    version : meson.project_version(),
    name : meson.project_name(),
    description : 'A SRT Streaming library',
    filebase : gaeguli_api_name,
    requires : [
      'gstreamer-1.0',
      'gstreamer-base-1.0',
      'gstreamer-net-1.0',
      'srt'
    ],
    requires_private : [
      'glib-2.0 ' + glib_req_version,
      'gobject-2.0 ' + glib_req_version,
      'gio-2.0 ' + glib_req_version,
      'gio-unix-2.0 ' + glib_req_version,
    ],
    subdirs: gaeguli_api_name,
    variables: 'exec_prefix=${prefix}'
  )

endif


libgaeguli_dep = declare_dependency(link_with: libgaeguli,
  include_directories: [ gaeguli_incs ],
  dependencies: [ gobject_dep, gio_dep, gst_dep ],
  sources: [ gaeguli_enums_h ],
)


