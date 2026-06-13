ftxui_dep = dependency('ftxui', required: false)

if ftxui_dep.found()
  executable('rook-tui',
    'main.cpp',
    dependencies: [
      librook_core_dep,
      ftxui_dep,
    ],
    install: true,
  )
endif
