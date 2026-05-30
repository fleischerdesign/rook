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

i18n = import('i18n')
gnome = import('gnome')

subdir('proto')
subdir('libs/rook-core')
subdir('src/rook-gui')
subdir('data')
subdir('po')
subdir('tests')
