gaeguli_provider_daemon_sources = [
  'gaeguli-provider.c',
]

gaeguli_provider_c_args = [
  '-DHAVE_CONFIG_H',
  '-D_GNU_SOURCE',
  '-DG_LOG_DOMAIN="gaeguli-daemon-providerd"',
]

conf_config = configuration_data()
conf_config.set('VERSION', '"@0@"'.format(meson.project_version()))
conf_config.set('gaeguli_provider_path', join_paths(gaeguli_bindir, 'gaeguli_provider'))
conf_install_dir = join_paths(get_option('sysconfdir'), 'gaeguli')

conf_config_uninstalled = conf_config
conf_config_uninstalled.set('gaeguli_provider_path',
  join_paths(meson.build_root(), 'gaeguli', 'daemon', 'gaeguli_provider'))

configure_file(input : 'gaeguli_provider.conf.in',
               output : 'gaeguli_provider.conf',
               configuration : conf_config,
               install_dir : conf_install_dir)

configure_file(input : 'gaeguli_provider.conf.in',
               output : 'gaeguli_provider-uninstalled.conf',
               configuration : conf_config_uninstalled)

executable('gaeguli_provider',
  gaeguli_provider_daemon_sources,
  install: true,
  c_args : gaeguli_provider_c_args,
  include_directories : [gaeguli_incs],
  dependencies : [libgaeguli_dep],
)

subdir('user')
subdir('test')