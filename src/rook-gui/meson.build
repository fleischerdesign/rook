rook_gui_sources = files(
  'main.cpp',
  'application.cpp',
  'window.cpp',
  'views/chat_view.cpp',
  'views/message_widget.cpp',
  'views/chat_sidebar.cpp',
  'views/preferences_window.cpp',
  'views/llm_settings_page.cpp',
  'views/provider_dialog.cpp',
  'views/voice_settings_page.cpp',
  'views/appearance_page.cpp',
)

rook_gui_deps = [
  dependency('gtkmm-4.0'),
  dependency('libadwaita-1'),
  librook_core_dep,
]

rook_gui = executable(
  'rook-gui',
  rook_gui_sources,
  dependencies: rook_gui_deps,
  install: true,
)
