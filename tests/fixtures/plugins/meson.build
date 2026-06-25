add_languages('c', native: false)

rook_plugin_inc = include_directories(
  '../../../libs/rook-core/include/rook/adapters/hook'
)

mock_hook = shared_module(
  'mock_hook',
  'mock_hook.c',
  include_directories: rook_plugin_inc,
  name_prefix: '',
  build_by_default: true,
)

broken_hook = shared_module(
  'broken_hook',
  'broken_hook.c',
  include_directories: rook_plugin_inc,
  name_prefix: '',
  build_by_default: true,
)
