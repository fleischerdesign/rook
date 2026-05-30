rook_gui_sources = files(
  'main.cpp',
  'application.cpp',
  'window.cpp',
  'views/chat_view.cpp',
  'views/message_widget.cpp',
  'views/chat_sidebar.cpp',
  'views/settings_dialog.cpp',
  'views/first_run_wizard.cpp',
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
