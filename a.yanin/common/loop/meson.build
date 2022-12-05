loop_deps = [
  dependency('threads'),
  modules['collections.string'],
  modules['collections.vec'],
  modules['error'],
  modules['log'],
  modules['posix'],
  modules['error-codes.adapter'],
  modules['posix.adapter'],
]

modules += {
  'loop': declare_dependency(
    include_directories: 'include',
    link_with: library('common.loop', [
        'src/loop.c',
      ],
      dependencies: loop_deps,
      include_directories: 'include'),
    dependencies: loop_deps,
  ),
}
