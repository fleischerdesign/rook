protoc = find_program('protoc', required: false)
protoc_gen_grpc = find_program('grpc_cpp_plugin', required: false)
proto_dep = declare_dependency()

if protoc.found() and protoc_gen_grpc.found()
  proto_srcs = custom_target(
    'generate_proto',
    input: 'rook/v1/service.proto',
    output: [
      'service.pb.cc',
      'service.pb.h',
      'service.grpc.pb.cc',
      'service.grpc.pb.h',
    ],
    command: [
      files('gen_proto.sh'),
      protoc.full_path(),
      protoc_gen_grpc.full_path(),
      '@OUTDIR@',
      '@INPUT@',
      '@CURRENT_SOURCE_DIR@',
    ],
  )

  proto_dep = declare_dependency(
    sources: proto_srcs,
    include_directories: include_directories('.'),
  )
endif
