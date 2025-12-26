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

# Generate Python protobuf and gRPC code from .proto files

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROTO_DIR="${SCRIPT_DIR}/../../../src/service/proto"
OUT_DIR="${SCRIPT_DIR}/../src/beebium/_proto"

# Ensure output directory exists
mkdir -p "$OUT_DIR"

echo "Generating protobuf code from $PROTO_DIR to $OUT_DIR..."

python -m grpc_tools.protoc \
    -I "$PROTO_DIR" \
    --python_out="$OUT_DIR" \
    --grpc_python_out="$OUT_DIR" \
    "$PROTO_DIR/video.proto" \
    "$PROTO_DIR/keyboard.proto" \
    "$PROTO_DIR/debugger.proto"

# Fix imports in generated files to use relative imports
# The generated code uses absolute imports which don't work with src layout
for file in "$OUT_DIR"/*_pb2_grpc.py; do
    if [[ -f "$file" ]]; then
        # Replace 'import xxx_pb2' with 'from . import xxx_pb2'
        sed -i '' 's/^import \(.*_pb2\) as/from . import \1 as/' "$file"
    fi
done

echo "Proto files generated successfully in $OUT_DIR"
