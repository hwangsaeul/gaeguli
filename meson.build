project('gaeguli', 'c',
  version: '1.99.0',
  license: 'Apache-2.0',
  meson_version: '>= 0.46',
  default_options: [ 'warning_level=1',
                     'buildtype=debugoptimized',
                     'c_std=gnu89',
                     'stdsplit=false' ])

project_version = meson.project_version()
version_arr = project_version.split('.')
project_version_major = version_arr[0].to_int()
project_version_minor = version_arr[1].to_int()
project_version_micro = version_arr[2].to_int()

apiversion = '2.0'
soversion = 0
curversion = project_version_minor * 100 + project_version_micro
libversion = '@0@.@1@.0'.format(soversion, curversion)
gaeguli_api_name = '@0@-@1@'.format(meson.project_name(), apiversion)

prefix = get_option('prefix')

cc = meson.get_compiler('c')

add_project_arguments('-Wdeclaration-after-statement', language: 'c')

cdata = configuration_data()
cdata.set_quoted('PACKAGE_STRING', meson.project_name())
cdata.set_quoted('PACKAGE_NAME', meson.project_name())
cdata.set_quoted('PACKAGE', meson.project_name())
cdata.set_quoted('VERSION', meson.project_version())

host_system = host_machine.system()
if host_system == 'linux'
  cdata.set('DEFAULT_VIDEO_SOURCE', 'GAEGULI_VIDEO_SOURCE_V4L2SRC')
  cdata.set('DEFAULT_VIDEO_RESOLUTION', 'GAEGULI_VIDEO_RESOLUTION_1920X1080')
  cdata.set_quoted('DEFAULT_VIDEO_SOURCE_DEVICE', '/dev/video0')
elif ['darwin'].contains(host_system)
  cdata.set('DEFAULT_VIDEO_SOURCE', 'GAEGULI_VIDEO_SOURCE_AVFVIDEOSRC')
  cdata.set('DEFAULT_VIDEO_RESOLUTION', 'GAEGULI_VIDEO_RESOLUTION_640X480')
  cdata.set_quoted('DEFAULT_VIDEO_SOURCE_DEVICE', '')
  cdata.set('HAVE_OSX', 1)
else
  cdata.set('DEFAULT_VIDEOSRC', 'GAEGULI_VIDEO_SOURCE_UNKNOWN')
  cdata.set('DEFAULT_VIDEO_RESOLUTION', 'GAEGULI_VIDEO_RESOLUTION_UNKNOWN')
  cdata.set_quoted('DEFAULT_VIDEO_SOURCE_DEVICE', '')
endif

cdata.set('DEFAULT_VIDEO_FRAMERATE', 15)
cdata.set('DEFAULT_VIDEO_BITRATE', 1000000)
cdata.set('DEFAULT_VIDEO_CODEC', 'GAEGULI_VIDEO_CODEC_H264')
cdata.set('DEFAULT_ENCODING_METHOD', 'GAEGULI_ENCODING_METHOD_GENERAL')

cdata.set('_GAEGULI_EXTERN', '__attribute__((visibility("default"))) extern')
cdata.set('LIBDIR', join_paths(get_option('prefix'), get_option('libdir')))


configure_file(output : 'config.h', configuration : cdata)

# Dependencies
glib_req_version = '>= 2.44.0'

glib_dep = dependency('glib-2.0', version: glib_req_version,
    fallback: ['glib', 'libglib_dep'])
gio_dep = [dependency('gio-2.0', version: glib_req_version,
                  fallback: ['glib', 'libgio_dep']),
           dependency('gio-unix-2.0', version: glib_req_version,
                  fallback: ['glib', 'libgio_dep'])]
gobject_dep = dependency('gobject-2.0', version: glib_req_version,
    fallback: ['glib', 'libgobject_dep'])

gst_req_version = '>= 1.16.0'

gst_dep = [
  dependency('gstreamer-1.0', version: gst_req_version,
    fallback: ['gstreamer', 'gst_dep']), 
  dependency ('gstreamer-base-1.0', version: gst_req_version,
    fallback: ['gstreamer', 'gst_base_dep']),
  dependency ('gstreamer-net-1.0', version: gst_req_version,
    fallback: ['gstreamer', 'gst_net_dep']),
  dependency ('gstreamer-mpegts-1.0', version: gst_req_version,
    fallback: ['gst-plugins-bad', 'gstmpegts_dep']),
  dependency ('gstreamer-codecparsers-1.0', version: gst_req_version,
    fallback: ['gst-plugins-bad', 'gstcodecparsers_dep'])
]

libsrt_dep = dependency('srt', version: '>=1.4.1')

gnome = import('gnome')

gaeguli_incs = include_directories('.', 'gaeguli')
gaeguli_doc_incs = include_directories('gaeguli')

common_ldflags = [ ]

subdir('gaeguli')
subdir('tests')
subdir('tools')
subdir('doc')

if meson.version().version_compare('>= 0.46.0')
  python3 = import('python').find_installation()
  run_command(python3, '-c',
    'import shutil; shutil.copy("hooks/pre-commit.hook", ".git/hooks/pre-commit")')
endif
