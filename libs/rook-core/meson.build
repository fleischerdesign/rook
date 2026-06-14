librook_core_sources = files(
  'src/domain/conversation.cpp',
  'src/domain/audio_pipeline.cpp',
  'src/core/domain_actor.cpp',
  'src/core/worker_pool.cpp',
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
  'src/adapters/mcp/http_sse_transport.cpp',
  'src/adapters/mcp/mcp_client.cpp',
  'src/adapters/mcp/mcp_server_manager.cpp',
  'src/adapters/mcp/mcp_client_adapter.cpp',
  'src/adapters/mcp/null_tool_port.cpp',
  'src/adapters/builtin/builtin_tool_port.cpp',
  'src/adapters/composite/composite_tool_port.cpp',
  'src/adapters/security/capability.cpp',
  'src/adapters/security/security_manager.cpp',
  'src/adapters/security/security_guarded_tool_port.cpp',
  'src/adapters/security/bwrap_executor.cpp',
  'src/adapters/hook/hook_registry.cpp',
  'src/adapters/hook/builtin_hooks.cpp',
  'src/adapters/hook/plugin_loader.cpp',
  'src/adapters/hook/plugin_hook_adapter.cpp',
  'src/adapters/hook/core_api_provider.cpp',
  'src/adapters/extension/extension_manifest.cpp',
  'src/adapters/extension/extension_manager.cpp',
  'src/adapters/audio/miniaudio_adapter.cpp',
  'src/adapters/audio/sherpa_tts_adapter.cpp',
  'src/adapters/audio/sherpa_asr_adapter.cpp',
  'src/adapters/audio/sherpa_wakeword_adapter.cpp',
  'src/adapters/audio/model_downloader.cpp',
  'src/sync/hlc.cpp',
  'src/sync/sync_engine.cpp',
)

libgrpc_dep = dependency('grpc++', required: false)
libprotobuf_dep = dependency('protobuf', required: false)

librook_core_deps = [
  dependency('nlohmann_json'),
  dependency('spdlog'),
  dependency('libcurl'),
  dependency('libsecret-1'),
  dependency('alsa'),
  dependency('libpulse', required: false),
]

grpc_available = libgrpc_dep.found() and libprotobuf_dep.found() and protoc.found() and protoc_gen_grpc.found()

if grpc_available
  librook_core_sources += files(
    'src/adapters/server/grpc_service_adapter.cpp',
    'src/adapters/client/grpc_client_llm_port.cpp',
    'src/adapters/client/grpc_client_store_port.cpp',
    'src/adapters/client/grpc_client_extension_port.cpp',
    'src/core/peer_manager.cpp',
    'src/core/sync_manager.cpp',
    'src/auth/jwt.cpp',
    'src/adapters/server/tenant_manager.cpp',
    'src/adapters/server/tenant_service_adapter.cpp',
  )
  librook_core_deps += [libgrpc_dep, libprotobuf_dep, proto_dep,
    dependency('openssl')]
endif

libonnx_dep = dependency('libonnxruntime', required: false)
if libonnx_dep.found()
  librook_core_deps += libonnx_dep
  add_project_arguments('-DROOK_HAS_ONNXRUNTIME', language: 'cpp')
endif

libsherpa_cxx = meson.get_compiler('cpp').find_library(
  'sherpa-onnx-cxx-api',
  required: false
)
libsherpa_c = meson.get_compiler('cpp').find_library(
  'sherpa-onnx-c-api',
  required: false
)
if libsherpa_cxx.found() and libsherpa_c.found()
  librook_core_deps += [libsherpa_cxx, libsherpa_c]
  add_project_arguments('-DROOK_HAS_SHERPA_NATIVE', language: 'cpp')
endif

librook_core_inc_list = [
  include_directories('include'),
  include_directories('../../vendor/miniaudio'),
]

librook_core = shared_library(
  'rook-core',
  librook_core_sources,
  include_directories: librook_core_inc_list,
  dependencies: librook_core_deps,
  install: true,
)

librook_core_dep = declare_dependency(
  link_with: librook_core,
  include_directories: librook_core_inc_list,
  dependencies: librook_core_deps,
)
