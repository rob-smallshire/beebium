#!/bin/bash
# Generate TypeScript stubs from proto files using ts-proto.
#
# Requires: protoc, ts-proto (installed via npm)
#
# Usage:
#   npm run generate-protos

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
REPO_ROOT="$PROJECT_DIR/../.."
OUT_DIR="$PROJECT_DIR/src/generated"

# Proto source directories (passed to protoc as -I on every invocation
# so any cross-proto imports resolve regardless of which proto owns them).
SERVICE_PROTO_DIR="$REPO_ROOT/src/service/proto"
AUN_PROTO_DIR="$REPO_ROOT/src/extensions/aun"
PICONET_PROTO_DIR="$REPO_ROOT/src/extensions/piconet"
RPC_SERIAL_PROTO_DIR="$REPO_ROOT/src/extensions/rpc-serial"
HOST_SERIAL_PROTO_DIR="$REPO_ROOT/src/extensions/host-serial"
FILE_PRINTER_PROTO_DIR="$REPO_ROOT/src/extensions/file-printer"
JOB_FILE_PRINTER_PROTO_DIR="$REPO_ROOT/src/extensions/job-file-printer"
TCP_PRINTER_PROTO_DIR="$REPO_ROOT/src/extensions/tcp-printer"
HOST_PARALLEL_PRINTER_PROTO_DIR="$REPO_ROOT/src/extensions/host-parallel-printer"
EXTENSION_UI_PROTO_DIR="$REPO_ROOT/src/core/extension-api/proto"

# ts-proto plugin path
PLUGIN="$PROJECT_DIR/node_modules/.bin/protoc-gen-ts_proto"

if [ ! -x "$PLUGIN" ]; then
    echo "Error: ts-proto plugin not found at $PLUGIN"
    echo "Run 'npm install' first."
    exit 1
fi

# Clean and recreate output directory
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

# Proto files to generate, paired with the directory containing each.
# Format: "<source-dir>:<filename>".
PROTOS=(
    "$SERVICE_PROTO_DIR:debugger.proto"
    "$SERVICE_PROTO_DIR:system.proto"
    "$SERVICE_PROTO_DIR:video.proto"
    "$SERVICE_PROTO_DIR:audio.proto"
    "$SERVICE_PROTO_DIR:keyboard.proto"
    "$SERVICE_PROTO_DIR:disc.proto"
    "$SERVICE_PROTO_DIR:econet.proto"
    "$SERVICE_PROTO_DIR:serial.proto"
    "$SERVICE_PROTO_DIR:tube.proto"
    "$SERVICE_PROTO_DIR:econet_transport.proto"
    "$SERVICE_PROTO_DIR:indicator.proto"
    "$SERVICE_PROTO_DIR:extension_rpc.proto"
    "$AUN_PROTO_DIR:aun.proto"
    "$PICONET_PROTO_DIR:piconet_service.proto"
    "$RPC_SERIAL_PROTO_DIR:rpc_serial.proto"
    "$HOST_SERIAL_PROTO_DIR:host_serial.proto"
    "$FILE_PRINTER_PROTO_DIR:file_printer.proto"
    "$JOB_FILE_PRINTER_PROTO_DIR:job_file_printer.proto"
    "$TCP_PRINTER_PROTO_DIR:tcp_printer.proto"
    "$HOST_PARALLEL_PRINTER_PROTO_DIR:host_parallel_printer.proto"
    "$EXTENSION_UI_PROTO_DIR:extension_ui.proto"
)

echo "Generating TypeScript stubs from proto files..."

for entry in "${PROTOS[@]}"; do
    proto_dir="${entry%%:*}"
    proto_filename="${entry##*:}"
    echo "  $proto_filename"
    protoc \
        --plugin="protoc-gen-ts_proto=$PLUGIN" \
        --ts_proto_out="$OUT_DIR" \
        --ts_proto_opt=outputServices=grpc-js \
        --ts_proto_opt=esModuleInterop=true \
        --ts_proto_opt=env=node \
        --ts_proto_opt=forceLong=number \
        --ts_proto_opt=useExactTypes=false \
        --ts_proto_opt=importSuffix=.js \
        -I "$SERVICE_PROTO_DIR" \
        -I "$AUN_PROTO_DIR" \
        -I "$PICONET_PROTO_DIR" \
        -I "$RPC_SERIAL_PROTO_DIR" \
        -I "$HOST_SERIAL_PROTO_DIR" \
        -I "$FILE_PRINTER_PROTO_DIR" \
        -I "$JOB_FILE_PRINTER_PROTO_DIR" \
        -I "$TCP_PRINTER_PROTO_DIR" \
        -I "$HOST_PARALLEL_PRINTER_PROTO_DIR" \
        -I "$EXTENSION_UI_PROTO_DIR" \
        "$proto_dir/$proto_filename"
done

echo "Done. Generated files in $OUT_DIR"
