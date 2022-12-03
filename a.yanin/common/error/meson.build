error_deps = [
  dependency('libbacktrace'),
  dependency('threads'),
  modules['collections.string'],
  modules['log'],
]

modules += {
  'error': declare_dependency(
    include_directories: 'include',
    link_with: library('common.error', [
        'src/backtrace.c',
        'src/error.c',
        'src/format.c',
      ],
      dependencies: error_deps,
      include_directories: 'include'),
    dependencies: error_deps,
  ),
}
