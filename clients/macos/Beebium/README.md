# Beebium macOS Client

Native macOS frontend for the Beebium BBC Micro emulator. Uses Metal for rendering and gRPC for communication with the emulator server.

## Requirements

- macOS 14.0+
- Xcode 15+ (for building)
- XcodeGen (`brew install xcodegen`)
- A running beebium server (`beebium-model-b` or `beebium-model-b-plus`)

## Building

The Xcode project is generated from `project.yml` using XcodeGen (see [Build System](#build-system) below for details).

### First Time

```bash
brew install xcodegen
cd clients/macos/Beebium
xcodegen generate
```

### Command Line

```bash
cd clients/macos/Beebium
xcodebuild -scheme Beebium -configuration Debug build
```

The built app is located at:
```
~/Library/Developer/Xcode/DerivedData/Beebium-*/Build/Products/Debug/Beebium.app
```

### Xcode

```bash
cd clients/macos/Beebium
xcodegen generate   # If project.yml changed or first time
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
├── StatusBarView.swift   # Status bar at bottom of window
├── VideoClient.swift     # gRPC video streaming client
├── KeyboardClient.swift  # gRPC keyboard input client
├── SystemClient.swift    # gRPC system info client
├── KeyboardMapper.swift  # macOS keycode to BBC matrix mapping
├── KeyboardMTKView.swift # Metal view with keyboard handling
├── MetalRenderer.swift   # Metal frame rendering
└── Generated/            # gRPC Swift stubs
    ├── video.pb.swift
    ├── video.grpc.swift
    ├── keyboard.pb.swift
    ├── keyboard.grpc.swift
    ├── system.pb.swift
    └── system.grpc.swift
```

## Status Bar

The status bar displays information about the connected emulator at the bottom of the window. Currently it shows the machine model (e.g., "BBC Model B+ 64K").

- **Toggle visibility**: View > Show Status Bar (Cmd+/)
- The preference is persisted across app launches using UserDefaults

## Regenerating gRPC Stubs

If the proto files change, regenerate only the protos that changed, and pass
only the outputs you need:

- A change that only adds or edits **message** fields or enums (no new RPC,
  no changed RPC signature) touches only `<name>.pb.swift`. Run `--swift_out`
  **only**, for that proto only. The `<name>.grpc.swift` service stub refers to
  message *types*, not fields, so it stays byte-for-byte identical — do not pass
  `--grpc-swift_out` for it.
- A **service-shape** change (a new RPC, or a changed request/response type)
  also rewrites `<name>.grpc.swift`, so pass both `--swift_out` and
  `--grpc-swift_out` for that proto.

```bash
# Message-only change (e.g. a new field):
protoc \
  --swift_out=Beebium/Generated \
  -I ../../../src/service/proto \
  ../../../src/service/proto/keyboard.proto

# Service-shape change (new/changed RPC) — note the explicit --plugin:
protoc \
  --plugin=protoc-gen-grpc-swift="$PINNED_GRPC_SWIFT_PLUGIN" \
  --swift_out=Beebium/Generated \
  --grpc-swift_out=Beebium/Generated \
  -I ../../../src/service/proto \
  ../../../src/service/proto/video.proto
```

### Use the pinned plugins, not Homebrew's

The committed stubs must be reproducible, so the generators have to match the
Swift package pins in
`Beebium.xcodeproj/project.xcworkspace/xcshareddata/swiftpm/Package.resolved`:

- **`protoc-gen-swift`** (for `--swift_out`): Homebrew's tracks swift-protobuf,
  which currently equals the pin (1.33.3), so `brew install swift-protobuf` is
  fine for message stubs.
- **`protoc-gen-grpc-swift`** (for `--grpc-swift_out`): Homebrew's version
  (1.23.1 at the time of writing) does **not** match the pinned grpc-swift
  (1.26.1), and regenerating a `.grpc.swift` with the wrong plugin rewrites the
  whole file in a different codegen style — thousands of lines of churn that
  bury the real change. Build the plugin from the pinned tag and point `protoc`
  at it explicitly:

  ```bash
  git clone --depth 1 --branch 1.26.1 https://github.com/grpc/grpc-swift \
    ~/tools/grpc-swift-1.26.1          # match Package.resolved's grpc-swift version
  swift build -c release --product protoc-gen-grpc-swift \
    --package-path ~/tools/grpc-swift-1.26.1
  export PINNED_GRPC_SWIFT_PLUGIN=~/tools/grpc-swift-1.26.1/.build/release/protoc-gen-grpc-swift
  ```

  Verify the plugin before trusting it: regenerate an **unchanged** `.grpc.swift`
  and confirm `git diff` shows no change. Only then regenerate the changed ones.
  After regenerating, run `python ../../../scripts/sync_protocol_fingerprint.py`
  (from the repo) so `ProtocolFingerprint.swift` matches the other clients.

## Build System

### XcodeGen

The Xcode project file (`Beebium.xcodeproj`) is **generated**, not tracked in version control. This is intentional:

- The `project.yml` spec file defines the project structure
- XcodeGen generates the `.xcodeproj` from this spec
- This avoids merge conflicts in the notoriously conflict-prone `.pbxproj` format
- CI regenerates the project on every build, ensuring consistency

**The `.xcodeproj` is in `.gitignore` - do not track it.**

### First-Time Setup

```bash
# Install XcodeGen
brew install xcodegen

# Generate the Xcode project
cd clients/macos/Beebium
xcodegen generate

# Now you can open in Xcode or build from command line
open Beebium.xcodeproj
```

### After Pulling Changes

If source files were added/removed, regenerate the project:

```bash
cd clients/macos/Beebium
xcodegen generate
```

### project.yml Structure

The `project.yml` file defines:

- **targets**: Beebium app and BeebiumTests
- **sources**: Swift source files (auto-discovered from directory)
- **dependencies**: Swift packages (grpc-swift, swift-protobuf)
- **settings**: Build settings, deployment target, signing

Key sections:
```yaml
targets:
  Beebium:
    type: application
    platform: macOS
    deploymentTarget: "14.0"
    sources:
      - Beebium           # All .swift files in this directory
    dependencies:
      - package: grpc-swift
      - package: swift-protobuf
```

### Adding New Source Files

1. Create the `.swift` file in the `Beebium/` directory
2. Run `xcodegen generate`
3. The file is automatically included (sources are directory-based)

### CI Build Process

GitHub Actions (`.github/workflows/ci.yml`) builds the client:

```yaml
- name: Install XcodeGen
  run: brew install xcodegen

- name: Generate Xcode project
  working-directory: clients/macos/Beebium
  run: xcodegen generate

- name: Build client
  working-directory: clients/macos/Beebium
  run: xcodebuild build -scheme Beebium ...
```

This ensures CI always builds from the spec, not a stale project file.

### Troubleshooting

**"File not found" errors after checkout:**
```bash
xcodegen generate  # Regenerate project with current files
```

**Xcode shows stale file references:**
```bash
# Close Xcode, regenerate, reopen
xcodegen generate
open Beebium.xcodeproj
```

**Build fails with missing Swift packages:**
```bash
# Xcode should resolve automatically, but if not:
xcodebuild -resolvePackageDependencies
```

## Display Rendering

The Metal renderer handles BBC Micro display geometry, including aspect ratio correction and line-doubling for non-interlaced modes.

### Frame Data Flow

```
Server                          Client
──────                          ──────
FrameBuffer ──► gRPC Frame ──► VideoClient ──► MetalRenderer ──► Metal Shader
  (logical      (+ metadata)    (extracts      (builds          (aspect ratio
   pixels)                       interlace)     Uniforms)        + scaling)
```

### Key Concepts

**Logical vs Display Resolution:**
- Server outputs logical pixels (e.g., 320×256 for MODE 1)
- `display_width`/`display_height` specify target dimensions
- Shader scales texture to display size via UV mapping

**Pixel Aspect Ratio (PAR):**
- BBC pixels are not square: PAR = 0.96
- Pixels are slightly narrower than tall
- Applied in vertex shader for aspect ratio calculation

**Line-Doubling:**
- Non-interlaced modes (MODE 0-6): 256 scanlines line-doubled to 512 effective
- Interlaced mode (MODE 7): 500 scanlines displayed as-is
- Determined by `field_order` in frame metadata

### Uniforms Structure

```swift
struct Uniforms {
    var drawableSize: SIMD2<Float>   // Window size
    var textureSize: SIMD2<Float>    // Logical frame dimensions
    var displaySize: SIMD2<Float>    // Target display dimensions
    var totalSize: SIMD2<Float>      // Display + borders
    var borderOffset: SIMD2<Float>   // Left/top border offset
    var parScale: Float              // 0.96 for BBC
    var interlaced: UInt32           // 1 = interlaced, 0 = progressive
    // ... border colors
}
```

### Aspect Ratio Calculation (Vertex Shader)

```metal
// Apply PAR to width
float contentWidth = uniforms.totalSize.x * uniforms.parScale;

// Line-doubling for non-interlaced modes
float contentHeight = uniforms.totalSize.y;
if (uniforms.interlaced == 0) {
    contentHeight *= 2.0;  // 256 → 512 effective height
}

// Calculate aspect ratio for letterbox/pillarbox
float contentAspect = contentWidth / contentHeight;
```

### Mode Examples

| Mode | Logical | Interlaced | Effective Height | Aspect |
|------|---------|------------|------------------|--------|
| MODE 7 | 480×500 | Yes | 500 | ~1.23 |
| MODE 0 | 640×256 | No | 512 | ~1.20 |
| MODE 1 | 320×256 | No | 512 | ~1.20 |
| MODE 2 | 160×256 | No | 512 | ~1.20 |

The ~2.4% aspect difference between MODE 7 and bitmap modes is physically correct—MODE 7 has 12 fewer scanlines (500 vs 512 effective).

### Texture Sampling

The fragment shader uses nearest-neighbor filtering for magnification (sharp pixels) and linear filtering for minification (prevents thin features like cursors from being skipped at certain window sizes):

```metal
constexpr sampler textureSampler(mag_filter::nearest,
                                  min_filter::linear,
                                  address::clamp_to_edge);
```

### Border Rendering

The shader renders four distinct border regions around the content area, useful for debugging timing issues:
- Left border: Dark red
- Right border: Dark green
- Top border: Dark blue
- Bottom border: Dark yellow

Border dimensions come from the server's tracking of blanking periods around the active display area
