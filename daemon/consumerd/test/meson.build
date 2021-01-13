gaeguli_consumer_client_sources = [
  'gaeguli-consumer-client.c',
]

gaeguli_consumer_client_c_args = [
  '-DHAVE_CONFIG_H',
  '-D_GNU_SOURCE',
  '-DG_LOG_DOMAIN="gaeguli-consumer-client"',
]

conf_config = configuration_data()
conf_config.set('VERSION', '"@0@"'.format(meson.project_version()))
conf_config.set('gaeguli_consumer_client_path', join_paths(gaeguli_bindir, 'gaeguli_consumer_client'))
conf_install_dir = join_paths(get_option('sysconfdir'), 'gaeguli')

conf_config_uninstalled = conf_config
conf_config_uninstalled.set('gaeguli_consumer_client_path',
  join_paths(meson.build_root(), 'gaeguli', 'daemon', 'gaeguli_consumer_client'))

executable('gaeguli_consumer_client',
  gaeguli_consumer_client_sources,
  install: true,
  c_args : gaeguli_consumer_client_c_args,
  include_directories : [gaeguli_incs],
  dependencies : [libgaeguli_dep],
)