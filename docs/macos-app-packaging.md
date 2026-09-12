# macOS App Packaging

How the Beebium macOS app (`clients/macos/Beebium`) embeds the headless
emulator servers, ROMs, presets and extensions into a self-contained `.app`.

## Background

Beebium's architecture is multi-process: the Swift/Metal frontend launches a
headless C++ server executable (`beebium-model-b`, `-plus`, `-plus-128k`,
`-romram`) and talks to it over gRPC. For a distributable app, those server
executables — and everything they load — travel inside the `.app`.

The embedded payload is the **static server bundle**: the same relocatable tree
that ships in the release tarball and the PyPI server wheels, copied verbatim.
The servers are built against a vcpkg **static** gRPC/protobuf/abseil stack, so
they carry no external dynamic dependencies: their only non-system references are
to each other and to two internal ABI dylibs, all resolved by
`@loader_path`/`@rpath` inside the tree. The same bytes run as the app's server,
the tarball, and the wheel.

## Bundle layout

The static tree is copied verbatim under `Contents/Resources/servers/`, so the
app carries the tarball's `bin/ lib/ share/beebium/` layout unchanged:

```
Beebium.app/Contents/
├── MacOS/Beebium                      # the Swift app
└── Resources/
    └── servers/                       # <-- the static server bundle, verbatim
        ├── bin/
        │   ├── beebium-model-b            (+ -plus, -plus-128k, -romram)
        │   └── extensions/
        │       └── <name>/
        │           ├── <name>.dylib      # the dlopened plugin
        │           └── manifest.json     # plugin manifest
        ├── lib/
        │   ├── libbeebium_extension_api.dylib
        │   └── libbeebium_extension_ui_proto.dylib
        └── share/beebium/
            ├── roms/                      # bundled ROMs
            └── presets/                   # machine presets + thumbnails
```

The server executables live in `bin/`. Each server resolves its plugins from
`<exe-dir>/extensions/` (i.e. `bin/extensions/`), its internal ABI dylibs via an
`@rpath` into `../lib`, and its ROMs and presets from `../share/beebium/` — all
relative to its own on-disk location, so the whole tree relocates intact into the
bundle with no path rewriting.

### Why `Resources/servers/`, not `Frameworks/`

`Contents/Frameworks/` is validated by `codesign` as **code-only**: the plugin
tree's `manifest.json` files (and the bundled ROMs/presets) trip it with *"code
object is not signed at all"* and the app-signing step fails. The payload is a
mix of executables, dylibs, and resource files, so it lives under `Resources/`,
where `codesign` seals it as ordinary bundle resources (nested Mach-O included).

## The embed build phase

The build phase **"Embed Static Server Bundle"** in
`clients/macos/Beebium/project.yml` runs when `BEEBIUM_SERVERS_BUILD_DIR` points
at an **installed static tree** (the directory containing `bin/beebium-model-b`).
It:

1. copies `bin/`, `lib/` and `share/` verbatim into
   `Contents/Resources/servers/` (clearing any previous payload first);
2. verifies the copied payload — no absolute-path leaks (only `/usr/lib` and
   `/System` are permitted; any `/opt/homebrew`, `/Users/` or `/usr/local`
   reference fails the build), and every Mach-O carries a valid signature.

There is **no dependency-graph rewriting**: the static binaries are already
self-contained and ad-hoc signed when the bundle is produced, so the embed is a
copy plus verification.

If `BEEBIUM_SERVERS_BUILD_DIR` is unset or does not point at a static tree, the
embed is **skipped**, and the app launches servers from the runtime development
fallback instead (see below). This is the normal state for day-to-day
development.

### How the Swift side resolves the payload

`PresetManager` (`clients/macos/Beebium/Beebium/Presets/PresetManager.swift`) is
the only place that resolves server/ROM/preset paths:

- `serversDirpath()` returns `Bundle.main.resourcePath + "/servers/bin"` when the
  bundle carries an embedded payload (probed by the presence of
  `servers/bin/beebium-model-b`); server executables are then
  `servers/bin/beebium-<model>`.
- `bundledRomDirpath()` → `servers/share/beebium/roms` (passed to the server as
  `--rom-dir` at launch).
- `bundledPresetsDirpath()` → `servers/share/beebium/presets`.
- Extensions need no Swift path: the server discovers them at
  `<exe-dir>/extensions/`.

## Building a distributable (embedded) app

Produce a static server tree, then build the app pointed at it:

```bash
# 1. Configure a vcpkg-static build (arm64 shown; see macos-bundle.yml for the
#    canonical flags and the x86_64 variant) and install it to a staging prefix.
cmake -B build-static \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=arm64-osx-static \
  -DVCPKG_HOST_TRIPLET=arm64-osx-static
cmake --build build-static --target beebium-servers
cmake --install build-static --prefix /tmp/beebium-servers-staging

# 2. Build the app with the embed phase pointed at that tree.
cd clients/macos/Beebium
xcodegen generate                       # if project.yml changed
BEEBIUM_SERVERS_BUILD_DIR=/tmp/beebium-servers-staging \
  xcodebuild build -scheme Beebium -configuration Release -destination 'platform=macOS'
```

Equivalently, the tree can come from a release bundle tarball
(`beebium-server-<version>-macos-<arch>.tar.gz`), extracted — that is exactly the
`bundle-macos-<arch>` artifact CI hands the app build.

### Day-to-day development (no embedding)

`scripts/build-macos-app.sh` builds the servers and the app **without** embedding.
The app then launches servers from the runtime development fallback in
`serversDirpath()`:

1. `BEEBIUM_SERVERS_DIRPATH` if set (the directory containing the executables);
2. otherwise `~/Code/beebium/build/src/server` (the CMake build tree).

Because the fallback runs the live build tree, the server can never go stale. For
a non-default build directory, export
`BEEBIUM_SERVERS_DIRPATH="$BUILD_DIR/src/server"` before launching.

### Verifying self-containment

```bash
APP=~/Library/Developer/Xcode/DerivedData/Beebium-*/Build/Products/Release/Beebium.app
SRV="$APP/Contents/Resources/servers"

# No dependency should point outside the bundle (only /usr/lib, /System OK):
find "$SRV/bin" "$SRV/lib" -type f | while read -r f; do
  file "$f" | grep -q Mach-O && otool -L "$f" | tail -n +2 \
    | grep -E '/opt/homebrew|/Users/|/usr/local' && echo "LEAK: $f"
done

# The signature must verify (ad-hoc at this stage):
codesign --verify --deep --strict "$APP"

# Plugin discovery through the bundled tree:
"$SRV/bin/beebium-model-b" list-extensions
```

## Distribution status

The bundle above is self-contained and runs on a clean machine, but it is
**ad-hoc signed** — dev-machine-only. Shipping it to other users requires
Developer ID signing with the Hardened Runtime, notarization and stapling, and
packaging as signed DMGs (plus a Homebrew cask). Those steps, and the automated
release wiring for them, are specified in
[docs/discussion/macos-gui-distribution.md](discussion/macos-gui-distribution.md).

## Related

- `docs/packaging.md` — how the server packages (Linux/macOS/Windows) and the
  clients are built and released.
- `docs/deployment.md` — ROM/preset discovery and the FHS install layout for the
  server executables outside the macOS app.
