subdir('resources')

sources = [
  'adaptor-demo.c',
  'http-server.c',
  'main.c',
  'traffic-control.c',
  gresources
]

executable('gaeguli-adaptor-demo', sources,
  dependencies: [
    libgaeguli_dep,
    json_glib_dep,
    dependency('libsoup-2.4'),
  ],
  install: true,
)

tc_helper_exe_name = 'gaeguli-tc-helper'
tc_helper_install_dir = get_option('libexecdir')

tc_helper_exe = executable(tc_helper_exe_name, 'tc-helper.c',
  dependencies: [
    dependency('libnl-route-3.0')
  ],
  install: true,
  install_dir: tc_helper_install_dir
)

meson.add_install_script('tc_helper_post_install.sh',
  tc_helper_install_dir / tc_helper_exe_name)
