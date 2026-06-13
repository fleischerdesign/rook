#!/usr/bin/env bash
set -euo pipefail

PROTOC="$1"
GRPC_PLUGIN="$2"
OUTDIR="$3"
INPUT="$4"
PROTO_DIR="$5"

"$PROTOC" \
  --proto_path="$PROTO_DIR" \
  --cpp_out="$OUTDIR" \
  --grpc_out="$OUTDIR" \
  --plugin="protoc-gen-grpc=$GRPC_PLUGIN" \
  "$INPUT"

if [ -d "$OUTDIR/rook/v1" ]; then
  mv "$OUTDIR/rook/v1/"*.pb.cc "$OUTDIR/" 2>/dev/null || true
  mv "$OUTDIR/rook/v1/"*.pb.h  "$OUTDIR/" 2>/dev/null || true
  rm -rf "$OUTDIR/rook"
fi

for f in "$OUTDIR/"*.pb.cc "$OUTDIR/"*.pb.h; do
  [ -f "$f" ] || continue
  sed -i 's|#include "rook/v1/|#include "|g' "$f"
done
