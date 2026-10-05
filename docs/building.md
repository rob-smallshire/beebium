# Building Beebium

This document describes how to build Beebium for different platforms and architectures.

## Overview

Beebium consists of two main components:

1. **Server** (C++20): The emulation core, built with CMake
2. **macOS Client** (Swift/SwiftUI): The GUI frontend, built with Xcode

Each component can be built for multiple architectures. We ship separate binaries for each architecture rather than Universal binaries.

## Prerequisites

### macOS

- Xcode 16+ (includes Swift 5.9, Metal, AppleClang 16+)
- CMake 3.16+
- Homebrew (for gRPC/protobuf)
- XcodeGen (`brew install xcodegen`)

### Linux

- GCC 10+ or Clang 12+
- CMake 3.16+
- gRPC and protobuf development libraries

### Windows

- Visual Studio 2022 (MSVC)
- CMake 3.16+
- vcpkg (for gRPC/protobuf)

## Building the Server

### macOS (Native Architecture)

Build for your Mac's native architecture (arm64 on Apple Silicon, x86_64 on Intel):

```bash
# Install dependencies
brew install cmake grpc protobuf

# Configure and build (Release)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Run tests
ctest --test-dir build --output-on-failure
```

Server executables are in `build/src/server/`:

| Executable | Machine |
|------------|---------|
| `beebium-model-b` | BBC Model B |
| `beebium-model-b-plus` | BBC Model B+ |
| `beebium-model-b-romram` | Model B with ROM/RAM board |
| `beebium-tube-65C02-3MHz` | 65C02 Tube co-processor |

To build all server executables at once, use the convenience target:

```bash
cmake --build build --target beebium-servers --parallel
```

### Debug vs Release Builds

CMake supports two main build types:

- **Release** (`-DCMAKE_BUILD_TYPE=Release`): Optimised for performance. Use this for normal emulator operation. The emulation loop runs with significant headroom at 2 MHz.
- **Debug** (`-DCMAKE_BUILD_TYPE=Debug`): Includes debug symbols, no optimisation. Use this during development for debugging with `lldb`. The emulation loop is substantially slower and may struggle to maintain 2 MHz with multiple peripherals enabled.

To maintain separate build trees for each:

```bash
# Debug build (for development)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel

# Release build (for performance)
cmake -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel
```

Both trees share the same source but produce independent binaries. You can switch between them freely.

ROM files are copied into the build tree during configuration. If your Release build is missing ROMs, either reconfigure or symlink them:

```bash
ln -sf ../build/roms build-release/roms
```

Extension plugins (for `--acorn-rtc`, `--acorn-scsi`, etc.) are also built per-tree. If running a server from one build tree but extensions were only built in another, use `--extension-dir` to point at them:

```bash
# Run Release server with Debug-built extensions
build-release/src/server/beebium-model-b-romram \
    --extension-dir build/src/extensions \
    --acorn-rtc layout=7bit-year-in-r7
```

Or build everything in the Release tree so extensions are available:

```bash
cmake --build build-release --parallel
```

### macOS (Cross-Architecture)

To build for a specific architecture (e.g., x86_64 on an Apple Silicon Mac):

```bash
# Install Rosetta 2 (if not already installed)
softwareupdate --install-rosetta

# Install x86_64 Homebrew and dependencies
arch -x86_64 /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
arch -x86_64 /usr/local/bin/brew install grpc protobuf

# Configure for x86_64
cmake -B build-x86_64 \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES=x86_64 \
    -DCMAKE_PREFIX_PATH=/usr/local

# Build
cmake --build build-x86_64 --parallel
```

Similarly, to build arm64 on an Intel Mac (requires macOS 11+ and appropriate toolchain):

```bash
cmake -B build-arm64 \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_PREFIX_PATH=/opt/homebrew
```

### Linux

```bash
# Install dependencies (Debian/Ubuntu)
sudo apt-get install cmake libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc

# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Run tests
ctest --test-dir build --output-on-failure
```

### Windows

```bash
# Using vcpkg for dependencies
cmake -B build ^
    -G "Visual Studio 17 2022" ^
    -A x64 ^
    -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake

cmake --build build --config Release --parallel

# Run tests
ctest --test-dir build --build-config Release --output-on-failure
```

## Building the macOS Client

### Generate Xcode Project

The Xcode project is generated from `project.yml` using XcodeGen:

```bash
cd clients/macos/Beebium
xcodegen generate
```

### Build for Native Architecture

```bash
xcodebuild build \
    -scheme Beebium \
    -destination 'platform=macOS' \
    -configuration Release \
    CODE_SIGNING_ALLOWED=NO
```

The app bundle is in `build/Release/Beebium.app` (path varies by Xcode settings).

### Build for Specific Architecture

To build for x86_64 (Intel):

```bash
xcodebuild build \
    -scheme Beebium \
    -destination 'platform=macOS,arch=x86_64' \
    -configuration Release \
    CODE_SIGNING_ALLOWED=NO
```

To build for arm64 (Apple Silicon):

```bash
xcodebuild build \
    -scheme Beebium \
    -destination 'platform=macOS,arch=arm64' \
    -configuration Release \
    CODE_SIGNING_ALLOWED=NO
```

### Run Tests

```bash
xcodebuild test \
    -scheme BeebiumTests \
    -destination 'platform=macOS' \
    CODE_SIGNING_ALLOWED=NO
```

## Architecture Summary

| Platform | Architecture | Runner/Machine | Notes |
|----------|--------------|----------------|-------|
| macOS | arm64 | Apple Silicon Mac, `macos-14` runner | Native on M1/M2/M3 |
| macOS | x86_64 | Intel Mac, `macos-15-intel` runner | Native on Intel, Rosetta on AS |
| Linux | x86_64 | `ubuntu-latest` runner | Standard Linux build |
| Windows | x64 | `windows-latest` runner | MSVC build |

## CI/CD

GitHub Actions builds and tests across all platforms. See `.github/workflows/ci.yml` for details.

**Server builds:**
- `server-macos-arm64`: Server for Apple Silicon
- `server-macos-x86_64`: Server for Intel
- `server-linux`: Server for Linux
- `server-windows`: Server for Windows

**GUI client builds:**
- `gui-client-macos-arm64`: Client for Apple Silicon
- `gui-client-macos-x86_64`: Client for Intel

**Client integration tests:**
- `typescript-client`, `typescript-integration-linux`, `typescript-integration-macos-arm64`, `typescript-integration-macos-x86_64`
- `python-client`, `python-integration-linux`, `python-integration-windows`, `python-integration-macos-arm64`, `python-integration-macos-x86_64`

## Deployment

After building, you need:

1. A server executable (`beebium-model-b`, `beebium-model-b-plus`, or `beebium-model-b-romram`)
2. The client app (`Beebium.app`)
3. ROM files in the expected location (see `docs/deployment.md`)
4. Extension plugins (in `src/extensions/` within the build tree, or specified via `--extension-dir`)

Both server and client must be built for the same architecture as the target Mac.

## Troubleshooting

### "Building for macOS-arm64 but attempting to link with file built for macOS-x86_64"

The dependencies (gRPC, protobuf) were built for a different architecture. Ensure you're using the correct Homebrew prefix:
- Apple Silicon: `/opt/homebrew`
- Intel: `/usr/local`

### Client won't launch on older Mac

Check the deployment target in `project.yml`. Currently set to macOS 13.0.

### Server crashes with "Illegal instruction"

The binary was built for a different CPU architecture. Ensure you're running the correct binary for your Mac's architecture.

### Extensions not found ("Unknown argument: --acorn-rtc")

Extension plugins must be built in the same build tree as the server, or pointed to explicitly with `--extension-dir`. If using a separate Release build tree, either build everything there (`cmake --build build-release --parallel`) or pass `--extension-dir` pointing to a build tree that has them.
