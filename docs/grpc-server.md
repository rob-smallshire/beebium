# Beebium gRPC Server Interface

Beebium provides model-specific server executables that run the BBC Micro emulator as a headless server, exposing video output, keyboard input, and debugging via gRPC.

## Available Executables

| Executable | Machine | MOS ROM | Description |
|------------|---------|---------|-------------|
| `beebium-model-b` | BBC Model B | MOS 1.20 | Original 32K BBC Micro |
| `beebium-model-b-plus` | BBC Model B+ 64K | MOS 2.0 | Enhanced 64K model |

Each executable contains only the hardware emulation needed for that machine type.

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
./src/server/beebium-model-b [options]
```

All arguments are optional. By default, the server loads the appropriate MOS ROM and BBC BASIC II for the machine type.

### Arguments

| Argument | Default | Description |
|----------|---------|-------------|
| `--mos <filepath>` | Machine-specific | Path to MOS ROM |
| `--rom <slot>:<filepath>` | Slot 15: bbc-basic_2.rom | Load ROM into sideways slot (0-15) |
| `--rom-dir <dirpath>` | Auto-detected | ROM directory path |
| `--port <port>` | 48875 | gRPC server port |
| `--info` | - | Print machine info as JSON and exit |
| `--help` | - | Show usage |

### Examples

```bash
# Use all defaults (recommended)
./beebium-model-b

# Replace BASIC with Forth in slot 15
./beebium-model-b --rom 15:forth.rom

# Add DFS in slot 14
./beebium-model-b --rom 14:dfs.rom

# Multiple ROMs
./beebium-model-b --rom 14:dfs.rom --rom 13:viewsheet.rom

# Override ROM directory
./beebium-model-b --rom-dir /my/roms

# Machine discovery
./beebium-model-b --info
```

The server runs until interrupted with Ctrl+C.

### Machine Discovery

The `--info` flag outputs machine information as JSON:

```json
{
  "executable": "beebium-model-b",
  "machine_type": "ModelB",
  "display_name": "BBC Model B",
  "version": "0.1.0",
  "default_mos_rom": "acorn-mos_1_20.rom",
  "default_language_rom": "bbc-basic_2.rom",
  "default_language_slot": 15
}
```

Frontends can use this to enumerate available machine types.

See [deployment.md](deployment.md) for ROM discovery and installation details.

## gRPC Services

### VideoService

Streams video frames from the emulator.

**Proto file:** `src/service/proto/video.proto`

#### GetConfig

Returns video configuration.

```bash
grpcurl -plaintext \
  -import-path src/service/proto -proto video.proto \
  localhost:48875 beebium.VideoService/GetConfig
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
  localhost:48875 beebium.VideoService/SubscribeFrames
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
  localhost:48875 beebium.KeyboardService/KeyDown

# Release the key
grpcurl -plaintext \
  -import-path src/service/proto -proto keyboard.proto \
  -d '{"row": 4, "column": 1}' \
  localhost:48875 beebium.KeyboardService/KeyUp
```

#### GetState

Returns the current keyboard matrix state.

```bash
grpcurl -plaintext \
  -import-path src/service/proto -proto keyboard.proto \
  localhost:48875 beebium.KeyboardService/GetState
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

channel = grpc.insecure_channel('localhost:48875')
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
                      │ gRPC (port 48875)
                      ▼
┌─────────────────────────────────────────────────┐
│  beebium-model-b / beebium-model-b-plus         │
│  ├── Server<MachineType>                        │
│  │   ├── VideoService (frame streaming)         │
│  │   ├── KeyboardService (input handling)       │
│  │   └── DebuggerService (memory, breakpoints)  │
│  └── Render thread (PixelBatch → FrameBuffer)   │
└─────────────────────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────┐
│  beebium_core                                   │
│  ├── Machine<CpuPolicy, MemoryPolicy>           │
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
