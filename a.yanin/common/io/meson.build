io_deps = [modules['error-codes'], modules['collections.string']]

modules += {
  'io': declare_dependency(
    include_directories: [include_directories('include'), conf_inc],
    link_with: library('common.io', 'src/io.c',
      dependencies: io_deps,
      include_directories: [include_directories('include'), conf_inc]),
    dependencies: io_deps,
  ),
}
