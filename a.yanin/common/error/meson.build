error_libbacktrace_dep = dependency('libbacktrace', required: get_option('libbacktrace'))

error_c_args = []

if error_libbacktrace_dep.found()
  error_c_args += ['-DBACKTRACE_ENABLED']
endif

error_deps = [
  error_libbacktrace_dep,
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
      c_args: error_c_args,
      dependencies: error_deps,
      include_directories: 'include'),
    dependencies: error_deps,
  ),
}
