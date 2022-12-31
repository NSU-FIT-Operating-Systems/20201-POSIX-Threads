error_codes_display_deps = [modules['error-codes']]

modules += {
  'error-codes.display': declare_dependency(
    include_directories: [include_directories('include'), conf_inc],
    link_with: library('common.error-codes.display', 'src/display.c',
      dependencies: error_codes_display_deps,
      include_directories: [include_directories('include'), conf_inc]),
    dependencies: error_codes_display_deps,
  ),
}
