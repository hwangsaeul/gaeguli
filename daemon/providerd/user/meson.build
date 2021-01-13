user_services_dir = join_paths(get_option('sysconfdir'), 'gaeguli')

install_data(
  install_dir : user_services_dir)

conf_config = configuration_data()
conf_config.set('GAEGULI_PROVIDER_BINARY', join_paths(gaeguli_bindir, 'gaeguli-provider'))

configure_file(input : 'gaeguli-provider.service.in',
               output : 'gaeguli-provider.service',
               configuration : conf_config,
               install_dir : user_services_dir)