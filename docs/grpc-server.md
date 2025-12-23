# beebium-server gRPC Interface

The `beebium-server` executable runs the BBC Micro emulator as a headless server, exposing video output and keyboard input via gRPC.

## Building

```bash
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/opt/homebrew  # macOS with Homebrew
make -j4
```

Requires gRPC and Protobuf installed:
```bash
brew install grpc protobuf  # macOS
```

## Running the Server

```bash
./src/server/beebium-server --mos <path/to/os12.rom> [options]
```

### Required Arguments

| Argument | Description |
|----------|-------------|
| `--mos <filepath>` | Path to MOS ROM (e.g., os12.rom) |

### Optional Arguments

| Argument | Default | Description |
|----------|---------|-------------|
| `--basic <filepath>` | - | Path to BASIC ROM (e.g., basic2.rom) |
| `--port <port>` | 50051 | gRPC server port |
| `--help` | - | Show usage |

### Example

```bash
./beebium-server --mos /path/to/os12.rom --basic /path/to/basic2.rom --port 50051
```

The server runs until interrupted with Ctrl+C.

## gRPC Services

### VideoService

Streams video frames from the emulator.

**Proto file:** `src/service/proto/video.proto`

#### GetConfig

Returns video configuration.

```bash
grpcurl -plaintext \
  -import-path src/service/proto -proto video.proto \
  localhost:50051 beebium.VideoService/GetConfig
```

Response:
```json
{
  "width": 736,
  "height": 576,
  "framerateHz": 50
}
```

#### SubscribeFrames

Streams frames as they complete (at VSYNC, ~50Hz).

```bash
grpcurl -plaintext \
  -import-path src/service/proto -proto video.proto \
  localhost:50051 beebium.VideoService/SubscribeFrames
```

Each frame contains:
- `frameNumber` - Monotonically increasing frame counter
- `width`, `height` - Frame dimensions (736×576)
- `pixels` - BGRA32 pixel data (~1.7MB per frame)

### KeyboardService

Controls the BBC keyboard matrix.

**Proto file:** `src/service/proto/keyboard.proto`

#### KeyDown / KeyUp

Press or release a key by matrix position.

```bash
# Press key at row 4, column 1
grpcurl -plaintext \
  -import-path src/service/proto -proto keyboard.proto \
  -d '{"row": 4, "column": 1}' \
  localhost:50051 beebium.KeyboardService/KeyDown

# Release the key
grpcurl -plaintext \
  -import-path src/service/proto -proto keyboard.proto \
  -d '{"row": 4, "column": 1}' \
  localhost:50051 beebium.KeyboardService/KeyUp
```

#### GetState

Returns the current keyboard matrix state.

```bash
grpcurl -plaintext \
  -import-path src/service/proto -proto keyboard.proto \
  localhost:50051 beebium.KeyboardService/GetState
```

Response:
```json
{
  "pressedRows": [0, 0, 0, 0, 2, 0, 0, 0, 0, 0]
}
```

Each element is a bitmask of pressed columns for that row.

## BBC Keyboard Matrix

The BBC Micro uses a 10×10 keyboard matrix. Common key positions:

| Key | Row | Column |
|-----|-----|--------|
| SHIFT | 0 | 0 |
| CTRL | 0 | 1 |
| A | 4 | 1 |
| B | 6 | 4 |
| RETURN | 4 | 9 |
| SPACE | 6 | 2 |

For the complete matrix, see `docs/keyboard-and-display.md`.

## Testing with C++

The test suite includes gRPC client tests:

```bash
cd build
./tests/test_grpc_video      # Video service tests
./tests/test_grpc_keyboard   # Keyboard service tests
```

These tests start a local server, connect as a gRPC client, and verify the services work correctly.

## Testing with Python

Generate Python stubs and connect:

```bash
# Generate Python stubs
python -m grpc_tools.protoc \
  -I src/service/proto \
  --python_out=. \
  --grpc_python_out=. \
  video.proto keyboard.proto

# Example client
python3 << 'EOF'
import grpc
import video_pb2
import video_pb2_grpc

channel = grpc.insecure_channel('localhost:50051')
stub = video_pb2_grpc.VideoServiceStub(channel)

# Get config
config = stub.GetConfig(video_pb2.GetConfigRequest())
print(f"Video: {config.width}x{config.height} @ {config.framerate_hz}Hz")

# Stream frames
for frame in stub.SubscribeFrames(video_pb2.SubscribeFramesRequest()):
    print(f"Frame {frame.frame_number}: {len(frame.pixels)} bytes")
    break  # Just get one frame
EOF
```

## Architecture

```
┌─────────────────────────────────────────────────┐
│  Client (grpcurl, Python, Swift, etc.)          │
└─────────────────────────────────────────────────┘
                      │ gRPC (port 50051)
                      ▼
┌─────────────────────────────────────────────────┐
│  beebium-server                                 │
│  ├── VideoService (frame streaming)             │
│  ├── KeyboardService (input handling)           │
│  └── Render thread (PixelBatch → FrameBuffer)   │
└─────────────────────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────┐
│  beebium_core                                   │
│  ├── Machine<ModelB> (emulation loop)           │
│  ├── OutputQueue<PixelBatch> (video pipeline)   │
│  └── SystemViaPeripheral (keyboard matrix)      │
└─────────────────────────────────────────────────┘
```

The server runs three threads:
1. **Main thread** - Emulation loop (~2MHz)
2. **Render thread** - Consumes PixelBatch queue, renders to FrameBuffer
3. **gRPC threads** - Handle client connections

## Frame Format

Frames are 736×576 pixels in BGRA32 format (4 bytes per pixel):
- Byte 0: Blue (0-255)
- Byte 1: Green (0-255)
- Byte 2: Red (0-255)
- Byte 3: Alpha (always 255)

Total frame size: 736 × 576 × 4 = 1,695,744 bytes (~1.7MB)

The frame includes overscan areas; the active display area depends on the video mode.
