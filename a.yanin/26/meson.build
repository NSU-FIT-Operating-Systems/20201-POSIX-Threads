project('quasi', 'c',
  default_options: ['warning_level=3', 'c_std=c2x', 'werror=true'])

common_modules = subproject('common', default_options: ['default_library=static']).get_variable('modules')

dependencies = [
  common_modules['collections.dlist'],
  common_modules['error.display'],
  common_modules['io'],
  common_modules['log'],
  common_modules['posix'],
  dependency('threads'),
]

common_sources = [
  'src/error.c',
  'src/quasi.c',
]

main_sources = common_sources + [
  'src/main.c',
]

include_dirs = include_directories('src')

c_args = ['-D_POSIX_C_SOURCE=200809L']

executable(meson.project_name(), main_sources,
  c_args: c_args,
  include_directories: include_dirs,
  dependencies: dependencies)
