executor_deps = [
  modules['error'],
  modules['log'],
]

modules += {
  'executor': declare_dependency(
    include_directories: [include_directories('include'), conf_inc],
    link_with: library('common.executor', [
        'src/executor.c',
      ],
      dependencies: executor_deps,
      include_directories: [include_directories('include'), conf_inc]),
    dependencies: executor_deps,
  ),
}
