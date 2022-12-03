collections_string_deps = [modules['error-codes'], modules['collections.vec']]

modules += {
  'collections.string': declare_dependency(
    include_directories: 'include',
    link_with: library('common.collections.string', 'src/string.c',
      dependencies: collections_string_deps,
      include_directories: 'include'),
    dependencies: collections_string_deps,
  ),
}
