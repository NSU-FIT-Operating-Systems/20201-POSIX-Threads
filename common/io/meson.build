io_deps = [modules['error'], modules['collections.string']]

modules += {
  'io': declare_dependency(
    include_directories: 'include',
    link_with: library('common.io', 'src/io.c',
      dependencies: io_deps,
      include_directories: 'include'),
    dependencies: io_deps,
  ),
}
