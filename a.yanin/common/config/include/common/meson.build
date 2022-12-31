common_conf = configuration_data()
common_conf.set('COMMON_PTHREADS_DISABLED', not pthreads_dep.found())
configure_file(output: 'config.h', configuration: common_conf)
