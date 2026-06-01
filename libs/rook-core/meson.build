librook_core_sources = files(
  'src/domain/event_bus.cpp',
  'src/domain/conversation.cpp',
  'src/domain/agent.cpp',
  'src/core/settings.cpp',
  'src/ports/provider_registry.cpp',
  'src/adapters/llm/llm_http_client.cpp',
  'src/adapters/llm/openai_compatible_adapter.cpp',
  'src/adapters/llm/openai_adapter.cpp',
  'src/adapters/llm/deepseek_adapter.cpp',
  'src/adapters/llm/ollama_adapter.cpp',
  'src/adapters/llm/anthropic_adapter.cpp',
  'src/adapters/llm/multi_provider_adapter.cpp',
  'src/adapters/llm/threaded_adapter.cpp',
  'src/adapters/model/model_cache.cpp',
  'src/adapters/model/model_discovery.cpp',
  'src/adapters/store/json_store.cpp',
  'src/adapters/secret_store.cpp',
  'src/adapters/mcp/stdio_transport.cpp',
)

librook_core_deps = [
  dependency('nlohmann_json'),
  dependency('spdlog'),
  dependency('libcurl'),
  dependency('libsecret-1'),
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
