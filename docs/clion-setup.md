# CLion Setup

This document describes how to configure CLion for developing Beebium on macOS.

## Prerequisites

- CLion (any recent version)
- Xcode Command Line Tools or full Xcode installation
- CMake 3.16+

## Opening the Project

1. `File` → `Open` → select the `beebium` directory
2. CLion will auto-detect the CMakeLists.txt and configure the project

## macOS: Fixing Standard Library Header Errors

On macOS, CLion's clangd may report errors like:

```
'cstdint' file not found
'string' file not found
```

This occurs because Apple's clang adds implicit SDK paths that clangd doesn't know about.

### Solution

The project includes a `.clangd` configuration file that tells clangd where to find the macOS SDK headers:

```yaml
CompileFlags:
  Add:
    - "-isysroot"
    - "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"
  Compiler: clang++
```

If you still see errors after opening the project:

1. Ensure the SDK path matches your Xcode installation:
   ```bash
   xcrun --show-sdk-path
   ```
2. Update `.clangd` if the path differs
3. `File` → `Invalidate Caches...` → check all boxes → `Invalidate and Restart`

### Non-Standard Xcode Location

If Xcode is installed elsewhere, update the `-isysroot` path in `.clangd` to match:

```yaml
CompileFlags:
  Add:
    - "-isysroot"
    - "/path/to/your/MacOSX.sdk"
  Compiler: clang++
```

## Build Configuration

CLion uses the CMake profile in `Preferences` → `Build, Execution, Deployment` → `CMake`.

Recommended profiles:

| Profile | CMake Options | Use Case |
|---------|---------------|----------|
| Debug | (default) | Development, debugging |
| Debug + Sanitizers | `-DBEEBIUM_ENABLE_SANITIZERS=ON` | Finding memory bugs |
| Release | `-DCMAKE_BUILD_TYPE=Release` | Performance testing |

## Toolchain

Check `Preferences` → `Build, Execution, Deployment` → `Toolchains`:

- **C Compiler**: `/usr/bin/clang` (or Xcode's clang)
- **C++ Compiler**: `/usr/bin/clang++`
- **CMake**: Bundled or system CMake 3.16+

## Running Tests

- Use the Run/Debug configurations auto-generated for each test target
- Or run from terminal: `cd build && ctest --output-on-failure`

## Troubleshooting

### Red underlines persist after setup

1. Ensure `compile_commands.json` exists in `build/`
2. Rebuild: `cmake -B build && cmake --build build`
3. `File` → `Reload CMake Project`
4. If still broken: `File` → `Invalidate Caches...` → `Invalidate and Restart`

### Wrong compiler detected

Check that `xcode-select` points to the correct developer tools:

```bash
xcode-select -p
# Should show: /Applications/Xcode.app/Contents/Developer
# Or: /Library/Developer/CommandLineTools
```
