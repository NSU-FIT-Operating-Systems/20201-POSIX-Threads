executor_deps = [
  dependency('threads'),
  modules['error'],
  modules['log'],
]

modules += {
  'executor': declare_dependency(
    include_directories: 'include',
    link_with: library('common.executor', [
        'src/executor.c',
      ],
      dependencies: executor_deps,
      include_directories: 'include'),
    dependencies: executor_deps,
  ),
}
