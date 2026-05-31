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
  'views/first_run_wizard.cpp',
)

rook_gui_deps = [
  dependency('gtk4'),
  dependency('libadwaita-1'),
  peel_dep,
  librook_core_dep,
]

rook_gui = executable(
  'rook-gui',
  rook_gui_sources,
  peel_bindings,
  dependencies: rook_gui_deps,
  install: true,
)
