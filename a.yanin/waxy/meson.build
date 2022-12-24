project('waxy', 'c',
  default_options: ['warning_level=3', 'c_std=c2x', 'werror=true', 'default_library=static'])

common_modules = subproject('common', default_options: ['default_library=static']).get_variable('modules')

picohttpparser = declare_dependency(
  include_directories: 'external',
  link_with: library('picohttpparser', 'external/picohttpparser/picohttpparser.c',
    include_directories: 'external/picohttpparser'),
)

dependencies = [
  common_modules['collections.dlist'],
  common_modules['collections.hash'],
  common_modules['error'],
  common_modules['error-codes.adapter'],
  common_modules['executor'],
  common_modules['io'],
  common_modules['log'],
  common_modules['loop'],
  common_modules['memory.arc'],
  common_modules['posix'],
  common_modules['posix.adapter'],
  picohttpparser,
  dependency('threads'),
]

st_dependencies = dependencies + [
  common_modules['executor.single'],
]

mt_dependencies = dependencies + [
  common_modules['executor.thread-pool'],
]

common_sources = [
  'src/cache.c',
  'src/client.c',
  'src/gai-adapter.c',
  'src/main.c',
  'src/server.c',
  'src/url.c',
  'src/upstream.c',
]

st_sources = [
  'src/executor-st.c',
]

mt_sources = [
  'src/executor-mt.c',
]

include_dirs = [
  include_directories('src'),
  include_directories('external', is_system: true),
]

c_args = [
  '-D_POSIX_C_SOURCE=200809L',
  '-D_XOPEN_SOURCE=700',
]

waxy_common = library('waxy-common', common_sources,
  c_args: c_args,
  dependencies: dependencies,
  include_directories: include_dirs)

executable('waxy-st', st_sources,
  c_args: c_args,
  include_directories: include_dirs,
  link_with: waxy_common,
  dependencies: st_dependencies)

executable('waxy-mt', mt_sources,
  c_args: c_args,
  include_directories: include_dirs,
  link_with: waxy_common,
  dependencies: mt_dependencies)
