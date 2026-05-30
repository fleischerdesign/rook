protoc = find_program('protoc', required: false)

if protoc.found()
  protoc_gen_grpc = find_program('grpc_cpp_plugin', required: false)

  if protoc_gen_grpc.found()
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
        protoc,
        '--proto_path=@CURRENT_SOURCE_DIR@',
        '--cpp_out=@OUTDIR@',
        '--grpc_out=@OUTDIR@',
        '--plugin=protoc-gen-grpc=' + protoc_gen_grpc.full_path(),
        '@INPUT@',
      ],
    )

    proto_dep = declare_dependency(
      sources: [proto_srcs[1]],  # .pb.h
    )
  endif
endif
