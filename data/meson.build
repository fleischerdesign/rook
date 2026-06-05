desktop_file = i18n.merge_file(
  input: 'io.github.fleischerdesign.Rook.desktop.in',
  output: 'io.github.fleischerdesign.Rook.desktop',
  type: 'desktop',
  po_dir: join_paths(meson.project_source_root(), 'po'),
  install: true,
  install_dir: join_paths(get_option('datadir'), 'applications'),
)

install_data(
  'icons/hicolor/scalable/apps/io.github.fleischerdesign.Rook.svg',
  install_dir: join_paths(get_option('datadir'), 'icons', 'hicolor', 'scalable', 'apps'),
)

install_data(
  'io.github.fleischerdesign.Rook.gschema.xml',
  install_dir: join_paths(get_option('datadir'), 'glib-2.0', 'schemas'),
)

gnome.compile_schemas(
  build_by_default: true,
  depend_files: files('io.github.fleischerdesign.Rook.gschema.xml'),
)
