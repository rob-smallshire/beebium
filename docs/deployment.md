# Beebium Deployment and Resource Discovery

This document describes how Beebium server executables are deployed and how they locate ROM files at runtime.

## ROM Files

Beebium requires ROM files to operate. These are not included in the repository due to copyright.

### ROM Naming Convention

ROMs use the format `<supplier>-<product>_<version>.rom`:

| Filename | Description | Size |
|----------|-------------|------|
| `acorn-mos_1_20.rom` | MOS 1.20 for BBC Model B | 16 KB |
| `acorn-mos_2_0.rom` | MOS 2.0 for BBC Model B+ | 16 KB |
| `bbc-basic_2.rom` | BBC BASIC II | 16 KB |

### Default ROMs per Machine

| Machine | Executable | MOS ROM | Language ROM (Slot 15) |
|---------|------------|---------|------------------------|
| BBC Model B | `beebium-model-b` | `acorn-mos_1_20.rom` | `bbc-basic_2.rom` |
| BBC Model B+ 64K | `beebium-model-b-plus` | `acorn-mos_2_0.rom` | `bbc-basic_2.rom` |

Slot 15 is the highest priority sideways ROM slot, conventionally used for the default language (BASIC from factory). Use `--rom 15:<filepath>` to replace BASIC with another language ROM.

## Directory Layouts

### Build Directory (Development)

After building, ROMs are copied to `build/roms/`:

```
build/
├── src/server/
│   ├── beebium-model-b
│   └── beebium-model-b-plus
└── roms/
    ├── acorn-mos_1_20.rom
    ├── acorn-mos_2_0.rom
    └── bbc-basic_2.rom
```

The executable locates ROMs via `../roms/` relative to its directory (i.e., `build/src/server/../roms/` = `build/roms/`).

### Installed Layout (FHS-Compliant)

When installed via `cmake --install`, Beebium follows the Filesystem Hierarchy Standard:

```
<prefix>/
├── bin/
│   ├── beebium-model-b
│   └── beebium-model-b-plus
└── share/beebium/roms/
    ├── acorn-mos_1_20.rom
    ├── acorn-mos_2_0.rom
    └── bbc-basic_2.rom
```

The default `<prefix>` is `/usr/local`. Override with:
```bash
cmake --install build --prefix /opt/beebium
```

The executable locates ROMs via `../share/beebium/roms/` relative to its directory.

## ROM Discovery Algorithm

At startup, Beebium searches for the ROM directory in this order:

1. **Explicit path**: `--rom-dir <dirpath>` command-line argument
2. **Environment variable**: `BEEBIUM_ROM_DIR`
3. **Build layout**: `<executable_dir>/../roms/`
4. **Installed layout**: `<executable_dir>/../share/beebium/roms/`
5. **Compile-time fallback**: `BEEBIUM_DEFAULT_ROM_DIR` (if defined)

The first existing directory wins.

### Individual ROM Resolution

When loading a ROM file (via `--mos` or `--rom`):

1. **Absolute path**: Used as-is (e.g., `/path/to/custom.rom`)
2. **Relative path with directory**: Resolved against current working directory (e.g., `./roms/custom.rom`)
3. **Simple filename**: Looked up in the ROM directory (e.g., `dfs.rom` becomes `<rom_dir>/dfs.rom`)

## Installation

### From Build Directory

```bash
# Build
mkdir build && cd build
cmake ..
make -j4

# Install to /usr/local (may require sudo)
sudo cmake --install .

# Or install to custom prefix
cmake --install . --prefix ~/.local
```

### Install Rules

The CMake install rules handle:
- Executables to `bin/`
- ROM files (`.rom`) to `share/beebium/roms/`

## Environment Variables

| Variable | Description |
|----------|-------------|
| `BEEBIUM_ROM_DIR` | Path to ROM directory (overrides auto-detection) |

Example:
```bash
export BEEBIUM_ROM_DIR=/path/to/my/roms
beebium-model-b
```

## Troubleshooting

### "Cannot find ROM directory"

The executable couldn't locate a ROM directory. Solutions:

1. Set the environment variable:
   ```bash
   export BEEBIUM_ROM_DIR=/path/to/roms
   ```

2. Use the command-line option:
   ```bash
   beebium-model-b --rom-dir /path/to/roms
   ```

3. Ensure ROMs are in the expected location relative to the executable.

### "ROM file not found"

A specific ROM file is missing. Check:

1. The file exists in the ROM directory
2. The filename matches exactly (case-sensitive on Unix)
3. For custom ROMs, use the full path or place in the ROM directory

### Verifying ROM Discovery

Use verbose mode to see which paths are checked:

```bash
# The server prints the ROM paths it loads
beebium-model-b
# Output:
# Loading MOS ROM: /path/to/roms/acorn-mos_1_20.rom
# Loading ROM into slot 15: /path/to/roms/bbc-basic_2.rom
# Initializing BBC Model B...
```

## Platform Notes

### macOS

- Install to `/usr/local` or `~/.local`
- Use Homebrew prefix if building with Homebrew dependencies:
  ```bash
  cmake .. -DCMAKE_PREFIX_PATH=/opt/homebrew
  ```

### Linux

- System-wide: `/usr/local` (FHS standard)
- User-local: `~/.local` (XDG Base Directory spec)
- The executable uses `/proc/self/exe` to find its own path

### Windows

- Portable: Keep executable and `roms/` folder together
- Installed: Use Program Files layout with `share/beebium/roms/`
- The executable uses `GetModuleFileName` to find its own path
