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
| `acorn-dfs_0_90.rom` | Acorn DFS 0.90 (8271) | 8 KB |
| `acorn-dfs_0_98.rom` | Acorn DFS 0.98 (8271) | 8 KB |
| `acorn-dnfs_1_00.rom` | Acorn DFS 1.00 + NFS 3.40 (8271) | 16 KB |
| `acorn-dnfs_1_20.rom` | Acorn DFS 1.20 + NFS 3.60 (8271) | 16 KB |
| `acorn-dnfs_3_02.rom` | Acorn DFS 1.22 + NFS 3.62 (8271) | 16 KB |

The combined DFS and NFS ROMs ("DNFS") are named by their DFS component
version. `acorn-dnfs_3_02.rom` is the exception: it is the DFS 1.22 release,
and keeps its older name for compatibility with existing presets and docs.
The 0.9x and 1.xx DFS releases need an Intel 8271 disc controller
(`--fdc acorn-8271`); DFS 2.26 needs the WD1770 (`--fdc acorn-1770`).

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

After building, ROMs are copied to `build/roms/` and the generated presets sit
beside the executables:

```
build/
├── src/server/
│   ├── beebium-model-b, beebium-model-b-plus, ...
│   ├── extensions/<name>/{<plugin library>, manifest.json}
│   ├── presets/   (*.beebium-preset)
│   └── blank-discs/   (copied from discs/blank/)
├── roms/
│   ├── acorn-mos_1_20.rom
│   ├── acorn-mos_2_0.rom
│   └── bbc-basic_2.rom, ...
```

The executable finds ROMs in the nearest `roms/` directory at or above its own
directory (`build/src/server/` -> `build/roms/`).

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
    ├── presets/   (*.beebium-preset)
    └── blank-discs/   (Acorn DFS 80T SS 200K.ssd, ...)
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
`../share/beebium/presets/` and blank disc templates via
`../share/beebium/blank-discs/`, relative to its directory.

## ROM Discovery Algorithm

At startup, Beebium searches for the ROM directory in this order:

1. **Explicit path**: `--rom-dir <dirpath>` command-line argument
2. **Environment variable**: `BEEBIUM_ROM_DIR`
3. **Build layout**: a `roms/` directory at or above `<executable_dir>` (searched upward a few levels, so a multi-config generator's per-config subdirectory still finds it)
4. **Installed layout**: `<executable_dir>/../share/beebium/roms/`
5. **Compile-time fallback**: `BEEBIUM_DEFAULT_ROM_DIR` (if defined)

The first existing directory wins. It is the system directory for ROMs: none
existing is not an error until a ROM is found nowhere else.

### Individual ROM Resolution

A ROM given by `--mos`, `--language-rom`, `--sideways ...:image=` or a preset
is found by the file reference rule in
[preset-schema.md](preset-schema.md#file-references): a bare name is looked
for beside the preset (or in the working directory, for the command line),
then in the per-user `roms/`, then in the ROM directory; a path is resolved
from the preset's directory (or the working directory), or from home with a
leading `~/`.

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
- System presets, each an archive holding its thumbnail and any disc it carries, to `share/beebium/presets/`
- Blank disc templates (`discs/blank/`) to `share/beebium/blank-discs/`

## Preset Discovery

The preset file format is described in [preset-schema.md](preset-schema.md).

System presets (shipped with the software) are searched in this order:

1. `$BEEBIUM_SERVERS_DIRPATH/presets/`
2. `<executable_dir>/presets/` (build layout)
3. `<executable_dir>/../share/beebium/presets/` (installed layout)

Every stored preset is a `<id>.beebium-preset` zip archive holding its
`preset.json`, its `thumbnail.png` and any file it carries (a disc, say). User presets (created with
`create-preset` / `import-preset`) live in
`presets/` in the per-user state directory (below). `list-presets --format jsonl` gives each preset's `filepath`.
A preset id is looked up among the system presets first, then the user
presets.

## Blank Disc Templates

A user who wants a new, empty floppy gets a copy of a shipped template: one
empty disc image per format, named so that the file name without its
extension is the label the user chooses it by (`Acorn DFS 80T SS 200K.ssd`,
`Acorn ADFS M 320K.adm`, ...). Beebium does not make disc formats itself, so
adding a format is adding a file. `list-blank-discs` lists them in file name
order and `create-disc` copies one into place (DiscService's `ListBlankDiscs`
and `CreateDisc` for clients); see [cli.md](cli.md#list-blank-discs).

The templates are committed under `discs/blank/`, made by
`scripts/make-blank-discs.sh` with a pinned `oaknut-disc` so the set is
byte-reproducible (`make-blank-discs.sh --check` verifies it). They are found
in this order, as the system presets are:

1. `$BEEBIUM_SERVERS_DIRPATH/blank-discs/`
2. `<executable_dir>/blank-discs/` (build layout)
3. `<executable_dir>/../share/beebium/blank-discs/` (installed layout)

## Bundled Discs and Machine Directories

Some presets need a hard-disc image -- for example the Level 3 File Server
preset needs its SCSI disc. Such a preset **carries** its disc: the image is
inside the preset's archive, beside `preset.json`, and the server ships no disc
images anywhere else. The disc is committed compressed
(`discs/bundled/<id>.tar.xz`, tiny because the image is almost entirely empty)
and extracted into the preset as the build puts it together. A user who wants
the Level 3 machine again duplicates its preset (`create-preset --from
model-b-l3fs-aun --name ...`, which takes the disc with it); one who wants the
image itself unpacks the preset (`export-preset model-b-l3fs-aun --unpack
--output <dir>`).

**Every running machine has a directory of its own**, under `machines/` in
the per-user directory, which it holds locked while it runs and which is
removed when it shuts down cleanly (a directory left by a machine that did
not is removed by the next launch, with one line saying so). Media a template
names by search or by position within itself is the machine's own copy;
media the user names by location is the user's own file. So:

- a disc found by search -- a bare name in the preset (beside its document) or
  in the per-user `discs/` -- is copied into the machine's directory and the
  copy is opened;
- a disc named by a path inside the preset (`discs/game.ssd`) is copied in
  too, at the same relative path;
- a disc named by an absolute or `~/` path outside the preset, given on the
  command line, or inserted while the machine runs is opened in place,
  read/write.

A bare name is looked for as any file reference is
([preset-schema.md](preset-schema.md#file-references)). To use a downloaded
image in a preset, drop its `.dat` and `.dsc` into the per-user `discs/` (or
beside the preset) and name it. The guest's writes to a copied disc last as
long as the machine: a machine started from a preset starts from the preset's
images at every launch, and keeps what it writes only once it is saved. ROMs
are only read, never copied.

**A saved machine** is a machine directory that outlives the server, kept
wherever the user puts it (the Documents folder by default in the macOS
app): made with `create-machine`, run with `start --machine`, or saved while
it runs (`SystemService.SaveMachine`, File > Save... in the macOS app). It
holds its own copies of its media, its configuration and identity, its last
screen and its battery-backed state, so every run continues from the last.
The server never chooses where one goes. Its layout and file formats are in
[machine-directory.md](machine-directory.md).

That per-user directory holds all of Beebium's per-user state: the user
presets in `presets/`, user-supplied disc and ROM images in `discs/` and
`roms/`, the directories of machines not yet saved in `machines/`, the shared AUN map file
`aun-map.json`, and the AUN automatic-station hint `aun-auto-next` (with its
lock file `aun-auto-next.lock`):

- macOS: `~/Library/Application Support/Beebium/`
- Windows: `%APPDATA%\Beebium\`
- Linux: `$XDG_CONFIG_HOME/beebium/` (or `~/.config/beebium/`)

`BEEBIUM_USER_DIRPATH` moves the whole directory, and the AUN files can be
moved separately (see Environment Variables). `report-aun-map-filepath`
prints the map file path in effect. For the map file and the hint file, see
`docs/networking.md`.

**Versioning**: bundled discs are version-named (e.g. `l3fs-v1_26b.dat`). A
revised image ships under a NEW name, so nobody can accidentally ship a
same-named image with different contents.

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

A preset references the disc it carries by its bare filename, which the
machine copies.

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
| `BEEBIUM_SERVERS_DIRPATH` | Directory whose `presets/` holds the system presets and whose `blank-discs/` holds the blank disc templates (overrides auto-detection) |
| `BEEBIUM_USER_DIRPATH` | The per-user state directory (overrides the platform default); its `presets/`, `discs/`, `aun-map.json` and `aun-auto-next` follow it. For tests and CI, to keep a run away from the real user's files |
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

A specific ROM file is missing. The message names every directory searched.
Check:

1. The file exists in one of them: beside the preset (or in the working
   directory), the per-user `roms/`, or the ROM directory
2. The filename matches exactly (case-sensitive on Unix)
3. For a ROM elsewhere, give its path

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
