posix_adapter_deps = [
  modules['error'],
  modules['posix'],
]

modules += {
  'posix.adapter': declare_dependency(
    include_directories: [include_directories('include'), conf_inc],
    link_with: library('common.posix.adapter', 'src/adapter.c',
      dependencies: posix_adapter_deps,
      include_directories: [include_directories('include'), conf_inc]),
    dependencies: posix_adapter_deps,
  ),
}
