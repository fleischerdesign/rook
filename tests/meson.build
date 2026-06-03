gtest_dep = dependency('gtest', main: true, required: false)

if gtest_dep.found()
  test_deps = [
    librook_core_dep,
    gtest_dep,
  ]

  test_executable = executable(
    'rook-tests',
    'unit/test_event_bus.cpp',
    'unit/test_conversation.cpp',
    'unit/test_capability.cpp',
    'unit/test_security_manager.cpp',
    'unit/test_extension.cpp',
    dependencies: test_deps,
    build_by_default: false,
  )

  test('rook-core-tests', test_executable)
endif
