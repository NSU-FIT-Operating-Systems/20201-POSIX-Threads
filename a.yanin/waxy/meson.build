project('waxy', 'c',
  default_options: ['warning_level=3', 'c_std=c2x', 'werror=true', 'default_library=static'])

common_modules = subproject('common', default_options: ['default_library=static']).get_variable('modules')

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
  dependency('threads'),
]

st_dependencies = dependencies + [
  common_modules['executor.single'],
]

mt_dependencies = dependencies + [
  # TODO
]

common_sources = [
  'src/main.c',
  'src/cache.c',
  'src/url.c',
]

st_sources = common_sources + [
  'src/executor-st.c',
]

mt_sources = common_sources + [
  'src/executor-mt.c',
]

include_dirs = include_directories('src')

c_args = [
  '-D_POSIX_C_SOURCE=200809L',
  '-D_XOPEN_SOURCE=700',
]

executable('waxy-st', st_sources,
  c_args: c_args,
  include_directories: include_dirs,
  dependencies: st_dependencies)

executable('waxy-mt', mt_sources,
  c_args: c_args,
  include_directories: include_dirs,
  dependencies: mt_dependencies)
