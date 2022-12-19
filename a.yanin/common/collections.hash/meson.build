collections_hash_deps = [modules['error-codes'], modules['collections.vec']]

modules += {
  'collections.hash': declare_dependency(
    include_directories: 'include',
    link_with: library('collections.collections.hash', [
        'src/byte_hasher.c',
      ],
      dependencies: collections_hash_deps,
      include_directories: 'include'),
    dependencies: collections_hash_deps,
  ),
}
