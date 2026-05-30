librook_core_sources = files(
  'src/domain/event_bus.cpp',
)

librook_core_deps = [
  dependency('nlohmann_json'),
  dependency('spdlog'),
]

librook_core_inc = include_directories('include')

librook_core = shared_library(
  'rook-core',
  librook_core_sources,
  include_directories: librook_core_inc,
  dependencies: librook_core_deps,
  install: true,
)

librook_core_dep = declare_dependency(
  link_with: librook_core,
  include_directories: librook_core_inc,
  dependencies: librook_core_deps,
)
