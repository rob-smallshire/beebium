# Beebium macOS Client

Native macOS frontend for the Beebium BBC Micro emulator. Uses Metal for rendering and gRPC for communication with the emulator server.

## Requirements

- macOS 14.0+
- Xcode 15+ (for building)
- A running beebium server (`beebium-model-b` or `beebium-model-b-plus`)

## Building

### Command Line

```bash
cd /Users/rjs/Code/beebium/clients/macos/Beebium
xcodebuild -scheme Beebium -configuration Debug build
```

The built app is located at:
```
~/Library/Developer/Xcode/DerivedData/Beebium-*/Build/Products/Debug/Beebium.app
```

### Xcode

```bash
open Beebium.xcodeproj
```

Then build with Cmd+B or run with Cmd+R.

## Running

1. Start the emulator server:
   ```bash
   cd /Users/rjs/Code/beebium/build
   ./src/server/beebium-model-b
   ```

2. Launch the client:
   ```bash
   open ~/Library/Developer/Xcode/DerivedData/Beebium-*/Build/Products/Debug/Beebium.app
   ```

   Or run directly:
   ```bash
   ~/Library/Developer/Xcode/DerivedData/Beebium-*/Build/Products/Debug/Beebium.app/Contents/MacOS/Beebium
   ```

The client connects to `localhost:48875` (0xBEEB) by default.

## Architecture

```
Beebium.app
├── BeebiumApp.swift      # App entry point
├── ContentView.swift     # Main window
├── EmulatorView.swift    # Emulator display container
├── VideoClient.swift     # gRPC video streaming client
├── KeyboardClient.swift  # gRPC keyboard input client
├── KeyboardMapper.swift  # macOS keycode to BBC matrix mapping
├── KeyboardMTKView.swift # Metal view with keyboard handling
├── MetalRenderer.swift   # Metal frame rendering
└── Generated/            # gRPC Swift stubs
    ├── video.pb.swift
    ├── video.grpc.swift
    ├── keyboard.pb.swift
    └── keyboard.grpc.swift
```

## Regenerating gRPC Stubs

If the proto files change:

```bash
protoc \
  --swift_out=Beebium/Generated \
  --grpc-swift_out=Beebium/Generated \
  -I ../../src/service/proto \
  ../../src/service/proto/video.proto \
  ../../src/service/proto/keyboard.proto
```

Requires `protoc` and `protoc-gen-grpc-swift`:
```bash
brew install swift-protobuf grpc-swift
```

## XcodeGen

The project uses [XcodeGen](https://github.com/yonaskolb/XcodeGen) for project file generation. If you modify `project.yml`:

```bash
brew install xcodegen
xcodegen generate
```
