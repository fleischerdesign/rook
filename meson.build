project(
  'rook',
  'cpp',
  version: '0.1.0',
  license: 'MIT',
  meson_version: '>= 1.4.0',
  default_options: [
    'warning_level=3',
    'werror=true',
    'cpp_std=c++20',
    'buildtype=debugoptimized',
  ],
)

add_global_arguments('-DVERSION="@0@"'.format(meson.project_version()), language: 'cpp')

libgrpc_dep = dependency('grpc++', required: false)
libprotobuf_dep = dependency('protobuf', required: false)
if libgrpc_dep.found() and libprotobuf_dep.found()
  add_project_arguments('-DROOK_HAS_GRPC', language: 'cpp')
endif

i18n = import('i18n')
gnome = import('gnome')

peel_dep = dependency('peel')
peel_gen = find_program('peel-gen')

peel_bindings = custom_target('peel-bindings',
  command: [peel_gen, '--recursive', '--out-dir', '@OUTDIR@', 'Gtk', '4.0', 'Adw', '1'],
  output: 'peel-bindings.stamp',
)

subdir('proto')
subdir('libs/rook-core')
subdir('src/rook-gui')
subdir('src/rookd')
subdir('src/rook-tui')
subdir('data')
subdir('po')
subdir('tests')

install_data('data/rook.sh',
  install_dir: get_option('bindir'),
  rename: 'rook',
  install_mode: ['rwxr-xr-x'],
)
