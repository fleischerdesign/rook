rookd_sources = files('main.cpp')

rookd_deps = [
  librook_core_dep,
]

if grpc_available
  rookd_deps += [libgrpc_dep, libprotobuf_dep, proto_dep]

  executable('rookd',
    rookd_sources,
    dependencies: rookd_deps,
    install: true,
  )
endif
