# Beebium Deployment and Resource Discovery

This document describes how Beebium server executables are deployed, how they locate ROMs, presets, extensions and bundled discs at runtime, and where they keep per-user state.

> For embedding the servers and their native dependencies inside the macOS `.app` bundle (and the path to a distributable, notarized build), see [macOS App Packaging](macos-app-packaging.md).

## ROM Files

Beebium requires ROM files to operate. The ROM images live in the repository's
`roms/` directory; the build copies the ones the shipped machines and presets
need into `build/roms/`, and `cmake --install` installs every `.rom` from
`roms/` into `share/beebium/roms/`. Tube coprocessor firmware is not in the ROM
directory: it ships inside each coprocessor plugin's `roms/` directory.

### ROM Naming Convention

ROMs use the format `<supplier>-<product>_<version>.rom`:

| Filename | Description | Size |
|----------|-------------|------|
| `acorn-mos_1_20.rom` | MOS 1.20 for BBC Model B | 16 KB |
| `acorn-mos_2_0.rom` | MOS 2.0 for BBC Model B+ | 16 KB |
| `bbc-basic_2.rom` | BBC BASIC II | 16 KB |
| `acorn-dfs_2_26.rom` | DFS 2.26 (1770) | 16 KB |

### Default ROMs per Machine

| Machine | Executable | MOS ROM | Default sideways ROMs |
|---------|------------|---------|-----------------------|
| BBC Model B | `beebium-model-b` | `acorn-mos_1_20.rom` | slot 15: `bbc-basic_2.rom` |
| Model B with ROM/RAM board | `beebium-model-b-romram` | `acorn-mos_1_20.rom` | slot 15: `bbc-basic_2.rom` |
| Model B with ATPL Sidewise | `beebium-model-b-atpl-sidewise` | `acorn-mos_1_20.rom` | slot 14: `bbc-basic_2.rom` |
| Model B with Watford ROM/RAM | `beebium-model-b-watford-rom-ram` | `acorn-mos_1_20.rom` | slot 15: `bbc-basic_2.rom` |
| Model B with Integra-B | `beebium-model-b-integra-b` | `acorn-mos_1_20.rom` | slot 3: `bbc-basic_2.rom`; slot 15: `computech-ibos_1_26.rom` |
| BBC Model B+ 64K | `beebium-model-b-plus` | `acorn-mos_2_0.rom` | slot 15: `bbc-basic_2.rom`; slot 11: `acorn-dfs_2_26.rom` |
| BBC Model B+ 128K | `beebium-model-b-plus-128k` | `acorn-mos_2_0.rom` | slot 15: `bbc-basic_2.rom`; slot 11: `acorn-dfs_2_26.rom` |

The language slot holds the default language (BASIC from the factory). Use
`--language-rom <filepath>` to put another language ROM in that slot, or
`--sideways slot=<n>:type=rom:image=<filepath>` to fill any slot. Each
executable's `start --help` lists its defaults.

## Directory Layouts

### Extensions Directory

Plugin extensions, including the Acorn 65C02 second processor
(`acorn-65c02-coprocessor`) and the Piconet Econet transport, are loaded from
`<exe-dir>/extensions/`, one directory per plugin holding its library, its
`manifest.json` and any firmware in `roms/`. (Some extensions, such as the AUN
transport, are built into the server.) `--extension-dir <path>` adds further
search directories, and `list-extensions` shows what resolves.
A server run from a build tree in which the plugins have not been built
(the `beebium-servers` target builds them all), or from an installed tree
missing `extensions/acorn-65c02-coprocessor/`, has no Tube coprocessor:
`--tube-65c02` is not recognised and the machine boots without a second
processor. The release artifacts, packages and the macOS app bundle all
ship the `extensions/` tree.

### Build Directory (Development)

After building, ROMs are copied to `build/roms/`, generated presets sit beside
the executables, and bundled disc masters are decompressed to `build/discs/`:

```
build/
├── src/server/
│   ├── beebium-model-b, beebium-model-b-plus, ...
│   ├── extensions/<name>/{<plugin library>, manifest.json}
│   └── presets/   (*.preset.beebium, *.thumbnail.png)
├── roms/
│   ├── acorn-mos_1_20.rom
│   ├── acorn-mos_2_0.rom
│   └── bbc-basic_2.rom, ...
└── discs/
    └── l3fs-v1_26b.dat, l3fs-v1_26b.dsc
```

The executable finds ROMs in the nearest `roms/` directory at or above its own
directory (`build/src/server/` -> `build/roms/`), and bundled discs likewise in
the nearest `discs/`.

### Installed Layout (FHS-Compliant)

When installed via `cmake --install`, the tree is relocatable and self-describing:

```
<prefix>/
├── bin/
│   ├── beebium-model-b, beebium-model-b-plus, ...
│   └── extensions/<name>/{<plugin>.so, manifest.json, roms/}
├── lib/
│   ├── libbeebium_extension_api.so
│   └── libbeebium_extension_ui_proto.so
└── share/beebium/
    ├── roms/      (acorn-mos_1_20.rom, ...)
    ├── presets/   (*.preset.beebium, *.thumbnail.png)
    └── discs/     (bundled disc masters, read-only)
```

The server binaries carry an `$ORIGIN`-relative `RPATH` (`@loader_path` on macOS),
so they resolve the extension ABI libraries in `lib/` from the installed tree.
Each binary finds its `extensions/`, ROMs, and presets relative to its own
on-disk location (resolved from the OS, not `argv[0]`, so a `/usr/bin` symlink
into the tree works too), so the whole tree relocates intact.

The default `<prefix>` is `/usr/local`. Override with:
```bash
cmake --install build --prefix /opt/beebium
```

The distributable packages ship this same tree, differing only in *where* they
place it: the `.deb` and `.rpm` install it under `/opt/beebium` with `/usr/bin` symlinks
onto the binaries, while the `.tar.gz` (and the Windows `.zip`) is a relocatable
single directory the user extracts anywhere and puts on `PATH`. Because discovery
is entirely relative to the binary's own location, all of these work unchanged.
For the self-contained, statically linked bundles (and how they are built,
validated, and shipped), see [Packaging and Distribution](packaging.md).

The executable locates ROMs via `../share/beebium/roms/`, presets via
`../share/beebium/presets/`, and disc masters via `../share/beebium/discs/`,
relative to its directory.

## ROM Discovery Algorithm

At startup, Beebium searches for the ROM directory in this order:

1. **Explicit path**: `--rom-dir <dirpath>` command-line argument
2. **Environment variable**: `BEEBIUM_ROM_DIR`
3. **Build layout**: a `roms/` directory at or above `<executable_dir>` (searched upward a few levels, so a multi-config generator's per-config subdirectory still finds it)
4. **Installed layout**: `<executable_dir>/../share/beebium/roms/`
5. **Compile-time fallback**: `BEEBIUM_DEFAULT_ROM_DIR` (if defined)

The first existing directory wins.

### Individual ROM Resolution

When loading a ROM file (via `--mos`, `--language-rom`, `--sideways ...:image=`, or a preset):

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
- Plugin extensions (library, `manifest.json`, firmware) to `bin/extensions/<name>/`
- The extension ABI libraries to `lib/`
- ROM files (`.rom`) to `share/beebium/roms/`
- System presets and their thumbnails to `share/beebium/presets/`
- Bundled disc masters (`.dat`, `.dsc`), read-only, to `share/beebium/discs/`

## Preset Discovery

System presets (shipped with the software) are searched in this order:

1. `$BEEBIUM_SERVERS_DIRPATH/presets/`
2. `<executable_dir>/presets/` (build layout)
3. `<executable_dir>/../share/beebium/presets/` (installed layout)

User presets (created with `create-preset` / `import-preset`) live in
`$BEEBIUM_USER_PRESETS_DIRPATH` if set, else `presets/` in the per-user state
directory (below). `report-presets-dirpath` prints the user presets directory.
A preset id is looked up among the system presets first, then the user
presets.

## Bundled Discs and Copy-on-Write

Some presets ship a hard-disc image -- for example the Level 3 File Server
preset carries a SCSI image. These **master** images live read-only alongside
the ROMs and presets:

- Build tree: `<build>/discs/`
- Installed: `<prefix>/share/beebium/discs/`

Masters are committed compressed (`discs/bundled/<id>.tar.xz`, tiny because the
images are almost entirely empty) and decompressed at build time. They are
**immutable**: the emulated filing system writes to disc, so the server never
opens a master; instead, on first use of a bundled image it copies the master
to a **per-user working copy** and opens that. This keeps a shipped image (or a
signed/sealed macOS app bundle) from ever being modified.

Working copies live in the per-user Beebium state directory. That one
directory holds all of Beebium's per-user state: the user presets in
`presets/`, the disc working copies in `discs/`, the shared AUN map file
`aun-map.json`, and the AUN automatic-station hint `aun-auto-next` (with its
lock file `aun-auto-next.lock`):

- macOS: `~/Library/Application Support/Beebium/`
- Windows: `%APPDATA%\Beebium\`
- Linux: `$XDG_CONFIG_HOME/beebium/` (or `~/.config/beebium/`)

Each location can be overridden separately (see Environment Variables); the
working-copy directory is replaced by `BEEBIUM_DISC_WORK_DIR`, which also
changes the copy policy. `report-aun-map-filepath` prints the map file path in
effect. For the map file and the hint file, see `docs/networking.md`.

**Reset a disc to its shipped state**: delete its working copy; the next boot
re-copies the pristine master.

**Versioning**: masters are version-named (e.g. `l3fs-v1_26b.dat`). A revised
image ships under a NEW name -- a master is never mutated in place, so nobody
can accidentally ship a same-named image with different contents.

**Regenerating a bundled disc**: the committed `.tar.xz` under `discs/bundled/`
is the build input, but it is regenerable from parts, not a hand-made blob. For
the Level 3 File Server image, `packaging/discs/build-l3fs-image.sh` rebuilds it
from `packaging/discs/l3fs-parts/` (just the file-server binary and our
unattended-start BASIC program -- the Library trees and users are generated by
`oaknut-disc`). The build is **byte-reproducible**. oaknut and its
dependencies are pinned to exact versions; every oaknut command runs with the
calendar frozen at a fixed date, and the AFS file datestamps are then pinned to
a fixed era constant, so no date in the image depends on when it was built; and
the tarball records owner 0, a fixed mode and a fixed mtime rather than
anything about the builder. The image inside is therefore byte-identical
wherever it is built, and the `.tar.xz` is byte-identical for a given Python
and liblzma. That is the maintenance check:
`packaging/discs/build-l3fs-image.sh --check` builds twice, and fails unless
the two tarballs are identical and their image is identical to the committed
one.

A preset references a bundled image by its bare filename (resolved to the
working copy). An explicit path with a directory component, or an absolute
path, is treated as the user's own image: opened in place, read/write, with no
copy-on-write.

Note: mDNS **advertisement** (`--advertise`) is a runtime/environment concern,
not machine configuration, so it is a `start` flag and never part of a preset
-- a CI boot of a server preset must not advertise, while the GUI launcher (and
CLI users) pass `--advertise` when they actually want the machine on the
network. So even a file-server preset carries no "advertise" setting; the
launcher enables it.

## Environment Variables

| Variable | Description |
|----------|-------------|
| `BEEBIUM_ROM_DIR` | Path to ROM directory (overrides auto-detection) |
| `BEEBIUM_SERVERS_DIRPATH` | Directory whose `presets/` holds the system presets (overrides auto-detection) |
| `BEEBIUM_USER_PRESETS_DIRPATH` | User presets directory (overrides the per-user default) |
| `BEEBIUM_DISC_DIR` | Path to the bundled disc **master** directory (overrides auto-detection) |
| `BEEBIUM_DISC_WORK_DIR` | Working-copy directory. When set, selects an ephemeral "scratch" mode that always re-copies the master fresh (used by the build to keep thumbnail capture deterministic and stateless); when unset, working copies persist in the per-user discs directory |
| `BEEBIUM_AUN_MAP_FILEPATH` | AUN map file path (overrides the per-user `aun-map.json`; `--aun map-file=` overrides this, and `map-file=none` disables the map file) |
| `BEEBIUM_AUN_AUTO_STATE_FILEPATH` | AUN automatic-station hint file (overrides the per-user `aun-auto-next`; `none` disables the hint) |

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
# Initializing BBC Model B...
# Loading MOS ROM: "/path/to/roms/acorn-mos_1_20.rom"
# Loading ROM into slot 15: "/path/to/roms/bbc-basic_2.rom"
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

- Portable: the `.zip` keeps the installed layout (`bin/`, `share/beebium/`) in one relocatable directory
- The executable uses `GetModuleFileName` to find its own path
