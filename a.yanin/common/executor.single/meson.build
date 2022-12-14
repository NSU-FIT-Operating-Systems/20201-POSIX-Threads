executor_single_deps = [
  dependency('threads'),
  modules['error'],
  modules['executor'],
  modules['log'],
  modules['collections.string'],
]

modules += {
  'executor.single': declare_dependency(
    include_directories: 'include',
    link_with: library('common.executor.single', [
        'src/single.c',
      ],
      dependencies: executor_single_deps,
      include_directories: 'include'),
    dependencies: executor_single_deps,
  ),
}
