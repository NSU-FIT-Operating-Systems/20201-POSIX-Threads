collections_log_deps = []

modules += {
  'log': declare_dependency(
    include_directories: 'include',
    link_with: library('common.log', 'src/log.c',
      dependencies: collections_log_deps,
      include_directories: 'include'),
    dependencies: collections_log_deps,
  ),
}
