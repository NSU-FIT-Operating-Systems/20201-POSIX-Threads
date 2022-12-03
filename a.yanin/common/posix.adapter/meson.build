posix_adapter_deps = [
  modules['error'],
  modules['posix'],
]

modules += {
  'posix.adapter': declare_dependency(
    include_directories: 'include',
    link_with: library('common.posix.adapter', 'src/adapter.c',
      dependencies: posix_adapter_deps,
      include_directories: 'include'),
    dependencies: posix_adapter_deps,
  ),
}
