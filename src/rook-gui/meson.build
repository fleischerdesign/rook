rook_gui_sources = files(
  'main.cpp',
  'application.cpp',
  'window.cpp',
  'views/chat_view.cpp',
  'views/message_widget.cpp',
  'views/markdown_renderer.cpp',
  'views/tool_call_row.cpp',
  'views/chat_sidebar.cpp',
  'views/preferences_window.cpp',
  'views/llm_settings_page.cpp',
  'views/provider_dialog.cpp',
  'views/mcp_settings_page.cpp',
  'views/mcp_server_dialog.cpp',
  'views/skill_dialog.cpp',
  'views/skills_page.cpp',
  'views/extension_settings_page.cpp',
  'views/extension_install_dialog.cpp',
  'views/voice_settings_page.cpp',
  'views/appearance_page.cpp',
  'views/first_run_wizard.cpp',
  'views/server_settings_page.cpp',
  'views/tray_icon.cpp',
  'views/permission_banner.cpp',
)

localedir = get_option('prefix') / get_option('localedir')

rook_gui_deps = [
  dependency('gtk4'),
  dependency('libadwaita-1'),
  dependency('libcmark-gfm'),
  peel_dep,
  librook_core_dep,
]

rook_gui = executable(
  'rook-gui',
  rook_gui_sources,
  peel_bindings,
  dependencies: rook_gui_deps,
  cpp_args: [
    '-DGETTEXT_PACKAGE="rook"',
    '-DLOCALEDIR="@0@"'.format(localedir),
  ],
  install: true,
)
