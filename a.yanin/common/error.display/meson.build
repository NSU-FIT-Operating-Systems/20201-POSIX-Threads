error_display_deps = [modules['error']]

modules += {
  'error.display': declare_dependency(
    include_directories: 'include',
    link_with: library('common.error.display', 'src/display.c',
      dependencies: error_display_deps,
      include_directories: 'include'),
    dependencies: error_display_deps,
  ),
}
