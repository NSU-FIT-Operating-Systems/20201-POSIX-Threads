loop_deps = [
  pthreads_dep,
  modules['collections.string'],
  modules['collections.vec'],
  modules['error'],
  modules['error-codes.adapter'],
  modules['executor'],
  modules['log'],
  modules['memory.arc'],
  modules['posix'],
  modules['posix.adapter'],
]

modules += {
  'loop': declare_dependency(
    include_directories: [include_directories('include'), conf_inc],
    link_with: library('common.loop', [
        'src/handler.c',
        'src/io.c',
        'src/loop.c',
        'src/notify.c',
        'src/pipe.c',
        'src/tcp.c',
      ],
      dependencies: loop_deps,
      include_directories: [include_directories('include'), conf_inc]),
    dependencies: loop_deps,
  ),
}
