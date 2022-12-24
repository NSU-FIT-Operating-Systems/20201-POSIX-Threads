executor_thread_pool_deps = [
  dependency('threads'),
  modules['collections.dlist'],
  modules['collections.vec'],
  modules['error'],
  modules['error-codes.adapter'],
  modules['executor'],
  modules['log'],
]

modules += {
  'executor.thread-pool': declare_dependency(
    include_directories: 'include',
    link_with: library('common.executor.thread-pool', [
        'src/thread-pool.c',
      ],
      dependencies: executor_thread_pool_deps,
      include_directories: 'include'),
    dependencies: executor_thread_pool_deps,
  ),
}
