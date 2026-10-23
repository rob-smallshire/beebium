#!/bin/bash
# Copyright 2025 Robert Smallshire <robert@smallshire.org.uk>
#
# This file is part of Beebium.
#
# Beebium is free software: you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version. Beebium is distributed in the hope that it will
# be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# You should have received a copy of the GNU General Public License along with Beebium.
# If not, see <https://www.gnu.org/licenses/>.

# Generate Python protobuf and gRPC code from .proto files.
#
# Core service stubs go to beebium/client/_proto; each extension's stubs go to
# beebium/ext/<name>/_proto, so an adapter package carries its own protos and
# can be split into a separate distribution later. There are no cross-file
# imports between these proto sets, so they are generated independently.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROTO_DIR="${SCRIPT_DIR}/../../../src/service/proto"
EXTENSIONS_DIR="${SCRIPT_DIR}/../../../src/extensions"
EXTENSION_API_PROTO_DIR="${SCRIPT_DIR}/../../../src/core/extension-api/proto"
SRC_DIR="${SCRIPT_DIR}/../src/beebium"
CORE_OUT="${SRC_DIR}/client/_proto"

HEADER='# Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
#
# This file is part of Beebium.
#
# Beebium is free software: you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version. Beebium is distributed in the hope that it will
# be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# You should have received a copy of the GNU General Public License along with Beebium.
# If not, see <https://www.gnu.org/licenses/>.
'

sed_inplace() {
    if sed --version >/dev/null 2>&1; then sed -i "$@"; else sed -i '' "$@"; fi
}

# Rewrite absolute stub imports to relative and inject the licence header, over
# every generated file in the given output directory.
postprocess() {
    local out="$1"
    # Rewrite the sibling _pb2 import to a package-relative one, in both the
    # runtime stubs (.py) and the mypy type stubs (.pyi).
    for file in "$out"/*_pb2_grpc.py "$out"/*_pb2_grpc.pyi; do
        [[ -f "$file" ]] || continue
        sed_inplace 's/^import \(.*_pb2\) as/from . import \1 as/' "$file"
        sed_inplace 's/^import \(.*_pb2\)$/from . import \1/' "$file"
    done
    for file in "$out"/*_pb2.py "$out"/*_pb2_grpc.py "$out"/*_pb2.pyi "$out"/*_pb2_grpc.pyi; do
        if [[ -f "$file" ]] && ! grep -q "Robert Smallshire" "$file"; then
            printf '%s\n%s' "$HEADER" "$(cat "$file")" > "$file"
        fi
    done
}

echo "Generating core service stubs to $CORE_OUT..."
mkdir -p "$CORE_OUT"
python -m grpc_tools.protoc \
    -I "$PROTO_DIR" \
    -I "$EXTENSION_API_PROTO_DIR" \
    --python_out="$CORE_OUT" \
    --grpc_python_out="$CORE_OUT" \
    --mypy_out="$CORE_OUT" \
    --mypy_grpc_out="$CORE_OUT" \
    "$PROTO_DIR/video.proto" \
    "$PROTO_DIR/keyboard.proto" \
    "$PROTO_DIR/debugger.proto" \
    "$PROTO_DIR/audio.proto" \
    "$PROTO_DIR/system.proto" \
    "$PROTO_DIR/disc.proto" \
    "$PROTO_DIR/econet.proto" \
    "$PROTO_DIR/econet_transport.proto" \
    "$PROTO_DIR/serial.proto" \
    "$PROTO_DIR/indicator.proto" \
    "$PROTO_DIR/sideways.proto" \
    "$PROTO_DIR/extension_rpc.proto" \
    "$PROTO_DIR/peripheral_extension.proto" \
    "$PROTO_DIR/tube.proto" \
    "$EXTENSION_API_PROTO_DIR/extension_ui.proto"
postprocess "$CORE_OUT"

# Each extension: <ext package name> <proto include dir> <proto file>
gen_ext() {
    local pkg="$1" proto_dir="$2" proto_file="$3"
    local out="${SRC_DIR}/ext/${pkg}/_proto"
    echo "Generating ${pkg} stubs to $out..."
    mkdir -p "$out"
    python -m grpc_tools.protoc \
        -I "$proto_dir" \
        --python_out="$out" \
        --grpc_python_out="$out" \
        --mypy_out="$out" \
        --mypy_grpc_out="$out" \
        "$proto_file"
    postprocess "$out"
}

gen_ext econet/aun          "$EXTENSIONS_DIR/aun"        "$EXTENSIONS_DIR/aun/aun.proto"
gen_ext econet/piconet      "$EXTENSIONS_DIR/piconet"    "$EXTENSIONS_DIR/piconet/piconet_service.proto"
gen_ext peripheral/rpc_serial  "$EXTENSIONS_DIR/rpc-serial" "$EXTENSIONS_DIR/rpc-serial/rpc_serial.proto"
gen_ext peripheral/host_serial "$EXTENSIONS_DIR/host-serial" "$EXTENSIONS_DIR/host-serial/host_serial.proto"
gen_ext peripheral/file_printer "$EXTENSIONS_DIR/file-printer" "$EXTENSIONS_DIR/file-printer/file_printer.proto"
gen_ext peripheral/tcp_printer "$EXTENSIONS_DIR/tcp-printer" "$EXTENSIONS_DIR/tcp-printer/tcp_printer.proto"
gen_ext peripheral/host_parallel_printer "$EXTENSIONS_DIR/host-parallel-printer" "$EXTENSIONS_DIR/host-parallel-printer/host_parallel_printer.proto"
gen_ext peripheral/acorn_rtc   "$EXTENSIONS_DIR/acorn-rtc"  "$EXTENSIONS_DIR/acorn-rtc/acorn_rtc.proto"
gen_ext peripheral/acorn_scsi  "$EXTENSIONS_DIR/acorn-scsi" "$EXTENSIONS_DIR/acorn-scsi/scsi_host_adapter.proto"

echo "Proto files generated successfully."
