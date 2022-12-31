error_codes_adapter_deps = [
  modules['error-codes'],
  modules['error-codes.display'],
  modules['error'],
]

modules += {
  'error-codes.adapter': declare_dependency(
    include_directories: [include_directories('include'), conf_inc],
    link_with: library('common.error-codes.adapter', 'src/adapter.c',
      dependencies: error_codes_adapter_deps,
      include_directories: [include_directories('include'), conf_inc]),
    dependencies: error_codes_adapter_deps,
  ),
}
