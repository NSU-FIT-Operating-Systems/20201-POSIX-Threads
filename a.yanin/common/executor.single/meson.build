executor_single_deps = [
  modules['error'],
  modules['executor'],
  modules['log'],
  modules['collections.string'],
]

modules += {
  'executor.single': declare_dependency(
    include_directories: [include_directories('include'), conf_inc],
    link_with: library('common.executor.single', [
        'src/single.c',
      ],
      dependencies: executor_single_deps,
      include_directories: [include_directories('include'), conf_inc]),
    dependencies: executor_single_deps,
  ),
}
