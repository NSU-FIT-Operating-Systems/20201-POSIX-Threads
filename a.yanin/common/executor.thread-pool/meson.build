if pthreads_dep.found()
  executor_thread_pool_deps = [
    pthreads_dep,
    modules['collections.dlist'],
    modules['collections.vec'],
    modules['error'],
    modules['error-codes.adapter'],
    modules['executor'],
    modules['log'],
  ]

  modules += {
    'executor.thread-pool': declare_dependency(
      include_directories: [include_directories('include'), conf_inc],
      link_with: library('common.executor.thread-pool', [
          'src/thread-pool.c',
        ],
        dependencies: executor_thread_pool_deps,
        include_directories: [include_directories('include'), conf_inc]),
      dependencies: executor_thread_pool_deps,
    ),
  }
endif
