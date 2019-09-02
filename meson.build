project('gaeguli', 'c',
  version: '0.9.0',
  license: 'LGPL',
  meson_version: '>= 0.51',
  default_options: [ 'warning_level=1',
                     'buildtype=debugoptimized' ])

project_version = meson.project_version()
version_arr = project_version.split('.')
project_version_major = version_arr[0].to_int()
project_version_minor = version_arr[1].to_int()
project_version_micro = version_arr[2].to_int()

apiversion = '1.0'
soversion = 0
curversion = project_version_minor * 100 + project_version_micro
libversion = '@0@.@1@.0'.format(soversion, curversion)
gaeguli_api_name = '@0@-@1@'.format(meson.project_name(), apiversion)

prefix = get_option('prefix')

cc = meson.get_compiler('c')

cdata = configuration_data()
cdata.set_quoted('PACKAGE_STRING', meson.project_name())
cdata.set_quoted('PACKAGE_NAME', meson.project_name())
cdata.set_quoted('PACKAGE', meson.project_name())
cdata.set_quoted('VERSION', meson.project_version())

# TODO: Check host_system
cdata.set('_GAEGULI_EXTERN', '__attribute__((visibility("default"))) extern')
cdata.set('LIBDIR', join_paths(get_option('prefix'), get_option('libdir')))

configure_file(output : 'config.h', configuration : cdata)

# Dependencies
glib_req_version = '>= 2.44.0'
gst_req_version = '>= 1.16.0'

glib_dep = dependency('glib-2.0', version: glib_req_version,
    fallback: ['glib', 'libglib_dep'])
gio_dep = [dependency('gio-2.0', version: glib_req_version,
                  fallback: ['glib', 'libgio_dep']),
           dependency('gio-unix-2.0', version: glib_req_version,
                  fallback: ['glib', 'libgio_dep'])]
gobject_dep = dependency('gobject-2.0', version: glib_req_version,
    fallback: ['glib', 'libgobject_dep'])

gnome = import('gnome')
python3 = import('python').find_installation()

gaeguli_incs = include_directories('.', 'gaeguli')

common_ldflags = [ ]

subdir('gaeguli')
subdir('tests')

run_command(python3, '-c', 'import shutil; shutil.copy("hooks/pre-commit.hook", ".git/hooks/pre-commit")')
