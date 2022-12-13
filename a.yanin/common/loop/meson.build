loop_deps = [
  dependency('threads'),
  modules['collections.string'],
  modules['collections.vec'],
  modules['error'],
  modules['error-codes.adapter'],
  modules['log'],
  modules['memory.arc'],
  modules['posix'],
  modules['posix.adapter'],
]

modules += {
  'loop': declare_dependency(
    include_directories: 'include',
    link_with: library('common.loop', [
        'src/executor.c',
        'src/handler.c',
        'src/io.c',
        'src/loop.c',
        'src/notify.c',
        'src/pipe.c',
        'src/tcp.c',
      ],
      dependencies: loop_deps,
      include_directories: 'include'),
    dependencies: loop_deps,
  ),
}
