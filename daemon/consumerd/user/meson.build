user_services_dir = join_paths(get_option('sysconfdir'), 'gaeguli')

install_data(
  install_dir : user_services_dir)

conf_config = configuration_data()
conf_config.set('GAEGULI_CONSUMER_BINARY', join_paths(gaeguli_bindir, 'gaeguli-consumer'))

configure_file(input : 'gaeguli-consumer.service.in',
               output : 'gaeguli-consumer.service',
               configuration : conf_config,
               install_dir : user_services_dir)