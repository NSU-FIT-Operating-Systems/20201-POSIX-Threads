collections_log_deps = [
  pthreads_dep,
]

modules += {
  'log': declare_dependency(
    include_directories: [include_directories('include'), conf_inc],
    link_with: library('common.log', 'src/log.c',
      dependencies: collections_log_deps,
      include_directories: [include_directories('include'), conf_inc]),
    dependencies: collections_log_deps,
  ),
}
