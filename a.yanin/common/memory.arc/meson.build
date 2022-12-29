memory_arc_deps = [
  modules['collections'],
]

memory_arc_c_args = []

modules += {
  'memory.arc': declare_dependency(
    include_directories: [include_directories('include'), conf_inc],
    dependencies: memory_arc_deps,
  ),
}
