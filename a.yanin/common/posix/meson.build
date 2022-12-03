posix_deps = [
  modules['error-codes'],
  meson.get_compiler('c').find_library('rt', required: false),
]

modules += {
  'posix': declare_dependency(
    include_directories: 'include',
    compile_args: '-D_POSIX_C_SOURCE=200809L',
    link_with: library('common.posix', [
        'src/aio.c',
        'src/dir.c',
        'src/file.c',
        'src/io.c',
        'src/ipc.c',
        'src/mem.c',
        'src/proc.c',
        'src/signal.c',
        'src/socket.c',
        'src/time.c',
        'src/tty.c',
      ], c_args: '-D_POSIX_C_SOURCE=200809L',
      dependencies: posix_deps,
      include_directories: 'include'),
    dependencies: posix_deps,
  ),
}
