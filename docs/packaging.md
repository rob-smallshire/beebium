# Packaging and Distribution

How the Beebium server is packaged for distribution, and how the clients are
distributed separately. The driving use case is running the headless emulator
as a **test environment for BBC Micro software in CI** (and locally): a user
installs the server, then drives it from the Python or TypeScript client.

For how an installed server discovers ROMs and presets at runtime, see
[Deployment and Resource Discovery](deployment.md). For embedding the servers in
the macOS `.app`, see [macOS App Packaging](macos-app-packaging.md).

## Distribution model

| Component | Channel | Status |
|-----------|---------|--------|
| Server (headless core) | Self-contained `.deb` (apt) + `.tar.gz` (everything else) | **Done** (Linux, both arches) |
| Server (macOS) | Homebrew tap `rob-smallshire/homebrew-beebium`, formula `beebium-server` | **Done** (arm64 CI-gated; Intel best-effort); tap publish is manual |
| Server (Windows) | Self-contained `.zip` (built + smoke-tested in CI, attached to the release) + Scoop bucket + WinGet | **Done** (`.zip` in CI/release); Scoop bucket + WinGet pending |
| Python client | PyPI (`beebium`) | Planned |
| TypeScript client | npm (`@beebium/client`) | Planned |

The server and the clients are deliberately distributed through **different**
channels (system package manager for the server, language ecosystems for the
clients). A user installs the server one way and `pip install beebium` /
`npm install @beebium/client` separately; that is expected and fine.

## Repositories and where things live

Packaging now spans **two repositories**. Knowing which is which avoids editing
the wrong copy of the formula.

### The monorepo — `rob-smallshire/beebium`

This repo (where you are reading) holds the source and **all the packaging
inputs**. Nothing here is the published Homebrew formula; this is where packaging
is authored and tested.

```
beebium/
├── CMakeLists.txt                 # install rules + CPack feed the packages
├── cmake/BeebiumPackaging.cmake   # CPack config (.deb + .tar.gz)
├── cmake/BeebiumCPackOptions.cmake# per-generator layout (deb=/opt, tgz=relocatable)
├── triplets/                      # static vcpkg triplets (linux/osx/windows-static-md)
├── docker/linux-bundle/           # static-bundle build (the Linux .deb/.tar.gz)
├── scripts/smoke-installed-tree.sh
├── packaging/
│   ├── debian/{postinst,prerm}    # .deb maintainer scripts
│   ├── smoke/test_smoke.py        # client-drives-installed-server interaction smoke
│   ├── windows/make-zip.ps1       # assemble the Windows .zip from the install tree
│   ├── scoop/
│   │   ├── beebium-server.json    # CANONICAL Scoop manifest (synced into the bucket)
│   │   └── sync-bucket.sh          # pin a released .zip into the Scoop bucket
│   └── homebrew/
│       ├── beebium-server.rb      # CANONICAL formula (source of truth, reviewed/CI'd)
│       ├── test-formula.sh         # build/install/test/audit against the working tree
│       └── sync-tap.sh             # pin a release into the tap (below)
└── .github/workflows/
    ├── linux-packages.yml         # build + smoke the Linux packages
    ├── macos-package.yml          # build/test/audit the Homebrew formula (arm64)
    ├── macos-intel.yml            # best-effort Intel formula check (non-gating)
    ├── windows-package.yml        # build + smoke the Windows .zip
    ├── release.yml                # tag -> all packages + verified draft Release
    └── release-smoke.yml          # post-publish end-to-end install from public channels
```

### The tap — `rob-smallshire/homebrew-beebium`

A **separate GitHub repo** that Homebrew clones when a user runs
`brew tap rob-smallshire/beebium`. It holds the **published** formula and the tap
README. Locally it lives under the Homebrew prefix, not beside this repo:

```
/opt/homebrew/Library/Taps/rob-smallshire/homebrew-beebium/
├── Formula/beebium-server.rb   # the PUBLISHED formula users install
├── README.md                   # tap landing page (install instructions, status)
└── .github/workflows/          # bottle-building CI (brew test-bot / pr-pull)
```

### How the two formulae stay in sync

`packaging/homebrew/beebium-server.rb` (monorepo) is the source of truth;
`Formula/beebium-server.rb` (tap) is a published copy pinned to a release. They
are connected by `packaging/homebrew/sync-tap.sh <version> <tap-checkout>`, which
fetches the release source tarball, computes its `sha256`, and writes the pinned
formula into the tap's `Formula/`. **Edit the canonical copy in the monorepo**,
validate it (`packaging/homebrew/test-formula.sh`), then run `sync-tap.sh` to push
the change to the tap — never hand-edit the tap's formula. (The `head` spec in the
canonical formula means `brew install --HEAD beebium-server` builds from the
monorepo's `master` directly, independent of any pinned release.)

## The self-contained static bundle (Linux)

The Linux server is shipped as a **self-contained, statically linked bundle**:
gRPC, protobuf, abseil, OpenSSL, re2, c-ares and zlib are linked **statically**
(via vcpkg), so the only dynamic dependencies are the base system libraries
(`libc`, `libstdc++`, `libgcc_s`, `libm`). This decouples the bundle from
whatever versions of those libraries a given distro ships, so one artifact per
architecture runs across distros.

### Why static, not a system-integrated `.deb`

A `.deb` that `Depends:` on the distro's `libgrpc++`/`libprotobuf` would tie the
server to each distro's library versions and couple it to the host's glibc
generation. The static bundle avoids both: it runs on any distro with a
new-enough glibc, which is exactly the heterogeneity CI inflicts. It also pins
the server's gRPC at build time, which is the right behaviour for the
cross-language compatibility story (see
[Versioning and Protocol Compatibility](versioning-and-compatibility.md)).

### The glibc floor

The bundle still links glibc dynamically (a fully static glibc breaks `dlopen`
and NSS, and Beebium `dlopen`s its plugins). A binary linked against an *older*
glibc runs on any *newer*-glibc system, so the bundle is built in a
**`debian:bookworm`** container (glibc 2.36). That floor covers Ubuntu 22.04+,
Raspberry Pi OS (Bookworm), and Arch (glibc 2.43+). Building natively on the
target distro would invert this and break portability, so we never do that.

### mDNS advertisement without a runtime dependency

The Linux server advertises itself over mDNS (DNS-SD `_beebium._tcp`) via Avahi,
so frontends auto-discover it. To keep the "only base libraries" property above,
`libavahi-client` is **loaded at runtime with `dlopen`**, not linked: the Avahi
headers are used at build time (the build container installs
`libavahi-client-dev`), but the shipped binary has no `libavahi-client`
dependency, so `dpkg-shlibdeps` does not add one to the `.deb`. When
`avahi-daemon` and `libavahi-client.so.3` are present at runtime (the default on
Raspberry Pi OS, Debian, and Ubuntu) the server advertises; otherwise it
degrades silently to a no-op. See
[Service Advertisement](plans/service-advertisement.md) for the advertiser's
design and [networking.md](networking.md) for the browse-side status.

### Architectures

Both `amd64` and `arm64` are first-class. The `arm64` CPU floor is the
Raspberry Pi 4 / 400 (Cortex-A72, baseline ARMv8-A): the build uses default
`aarch64` codegen with no `-mcpu` tuning, so one `arm64` bundle runs across
Pi 4 / 400 / 5. Raspberry Pi support requires a 64-bit OS image (Pi OS 64-bit or
Ubuntu for Pi); the 32-bit default image is not supported.

## The macOS Homebrew formula

The macOS server is distributed through a Homebrew tap
(`rob-smallshire/homebrew-beebium`, formula `beebium-server`). Unlike the Linux
bundle, it is a **source build against Homebrew's own grpc/protobuf/abseil** —
the idiomatic Homebrew approach — rather than a static bundle.

### Tap layout and naming

A tap is a single GitHub repo (`homebrew-<name>`; the `homebrew-` prefix is
stripped in `brew` commands) that can hold any number of formulae and casks. One
repo — `rob-smallshire/homebrew-beebium`, tapped as `rob-smallshire/beebium` —
holds **everything Beebium ships through Homebrew**; separate repos are not
needed:

```
homebrew-beebium/
├── Formula/
│   └── beebium-server.rb     # headless backend (CLI, build-from-source)
└── Casks/
    └── beebium-gui.rb        # macOS GUI app (.app bundle, future, out of scope now)
```

The packages are installed independently:

```
brew install rob-smallshire/beebium/beebium-server   # backend only
brew install --cask rob-smallshire/beebium/beebium-gui   # frontend only (future)
```

**Naming decision:** the backend is `beebium-server` and the macOS GUI will be
`beebium-gui`; the bare `beebium` token is left unclaimed. The names are
deliberately symmetric and descriptive so neither component squats the project's
bare name — consistent with Beebium's headless-core identity, where the GUI is
one of several frontends rather than "the" application. (A cask token
conventionally mirrors an app's display name, which would pull the GUI toward a
bare `beebium`; that is rejected here precisely because casks are macOS-only and
the bare name should not resolve to one platform's frontend.) The only hard
Homebrew constraint is that a formula and a cask in the same tap must not share a
token, or `brew install <token>` becomes an ambiguous formula-vs-cask choice;
distinct `-server`/`-gui` tokens avoid that entirely. The macOS app currently
embeds its own server binaries; if that changes, the cask can
`depends_on formula: "rob-smallshire/beebium/beebium-server"` while staying
separately installable.

### Why source-build, not static

Static-linking gRPC was historically forced on macOS by the "duplicate gRPC
runtime" crash: when both the server and a dlopened plugin embedded gRPC/abseil,
serving a plugin-hosted service segfaulted in `ExecCtx::Run`. Since the
**ExtensionRpc channel** landed, plugins no longer host gRPC services — they link
only `libbeebium_extension_api` and the shared `libprotobuf`, so only the core
links gRPC. The duplicate-runtime hazard is gone, and the formula can simply
`depends_on "grpc"` and friends and build from source.

The build needs no special toolchain: `CMakeLists.txt` already prefers CONFIG-mode
`find_package(Protobuf)` / `find_package(gRPC)` (Homebrew's), and
`nlohmann_json` is resolved the same way (`find_package(nlohmann_json CONFIG)`),
falling back to the pinned `FetchContent` copy only when no package is installed
— Homebrew's build sandbox forbids `FetchContent` network access, so the packaged
`nlohmann-json` is used there.

### Accepted gRPC-skew trade-off

A Homebrew-installed server links Homebrew's gRPC, which floats with the tap's
`grpc` formula, while a client may be installed independently (uv/pip, npm) or on
a different host/OS entirely. That skew is acceptable: gRPC keeps the wire
protocol cross-version compatible, and the connect-time **protocol-fingerprint
handshake** (see [Versioning and Protocol
Compatibility](versioning-and-compatibility.md)) rejects any *schema* mismatch.
Server and client version numbers need not match across the wire.

### Keg layout

The formula installs the whole relocatable tree under `libexec` and symlinks the
four servers into `bin`, so only the servers land on the user's `PATH` (not
`bin/extensions/`):

```
<keg>/
├── bin/{beebium-model-b, ...}            # symlinks -> ../libexec/bin/<server>
└── libexec/
    ├── bin/{beebium-model-b, ...}        # the real binaries
    ├── bin/extensions/<name>/{<plugin>.dylib, manifest.json}
    ├── lib/{libbeebium_extension_api.dylib, libbeebium_extension_ui_proto.dylib}
    └── share/beebium/{roms,presets}/
```

Discovery follows the `bin` symlink to the real binary's on-disk location (via
`_NSGetExecutablePath`), then resolves extensions, the ABI dylibs (via the
`@loader_path/../lib` install RPATH), ROMs and presets relative to it — the same
mechanism the Linux `/usr/bin` symlinks rely on.

### Files and validation

- `packaging/homebrew/beebium-server.rb` — the canonical formula, kept in the
  monorepo for review and CI.
- `packaging/homebrew/test-formula.sh` — packages the working tree into a
  GitHub-style source tarball, pins a throwaway formula at it, then
  `brew install --build-from-source` + `brew test` + `brew audit --strict` +
  an extension-discovery check. Run locally or in CI; both run identical steps.
- `packaging/homebrew/sync-tap.sh <version> <tap-checkout>` — fetches the
  released source tarball, computes its `sha256`, and writes the pinned formula
  into the tap's `Formula/` for the maintainer to commit and push.

Publishing the tap is deliberately manual: author + validate here, then push the
formula to `rob-smallshire/homebrew-beebium` to go live.

### Current state and remaining macOS work

The tap (`rob-smallshire/homebrew-beebium`) is **live and synced to `v0.1.0`**:
the formula's `url` points at the `v0.1.0` source tarball with the real pinned
`sha256` (`packaging/homebrew/sync-tap.sh`), so the **stable
`brew install beebium-server`** path works today — it builds from source, puts the
four servers on `PATH`, and discovers all extensions. This is validated
end-to-end on a clean runner by the macOS leg of `release-smoke.yml`. (The
`--HEAD` build-from-`master` path also works, via the formula's `head` URL.)

The one remaining piece is performance, not availability:

- **Bottles.** Until bottles exist, every `brew install` compiles beebium-server
  from source (~1 min locally, a few minutes on slower runners) — acceptable for
  dev, undesirable at CI scale. The `brew tap-new` scaffolding already added a
  bottle-building workflow to the tap; enable it (PR -> `brew test-bot` builds
  bottles per macOS runner/arch -> `pr-pull` uploads them and adds the `bottle do`
  block to the formula). After that, installs pour a pre-built binary and only
  unsupported macOS versions (or `--build-from-source`) compile.

A self-contained macOS `.tar.gz` (Homebrew-free, for macOS CI that wants the same
download-and-run experience as Linux) is a possible later addition; it needs the
vcpkg-static build path rather than the Homebrew-deps build.

## Windows

The Windows server is shipped as a **self-contained `x64-windows-static-md`
ZIP**, built, smoke-tested and attached to the release **in CI**
(`windows-package.yml`), and installed via a **Scoop bucket**. The approach
mirrors the Linux/macOS strategy — a self-contained artifact plus a
package-manager front end. What remains is the external Scoop bucket (and WinGet,
signing); the `.zip` itself is done end-to-end.

Note the two distinct Windows builds: the **normal CI** (`ci.yml`) still links
the vcpkg `x64-windows` libraries *dynamically* (copying the gRPC/protobuf/abseil
DLLs next to the executables to run the test suite); the **packaging** build uses
the static triplet below so the shipped `.zip` is self-contained.

### Server: a self-contained `.zip`

The primary artifact is a **self-contained ZIP**, the direct analogue of the Linux
`.tar.gz`: extract, put on `PATH`, run — no installer, no admin, ideal for CI. It
is produced by the same CPack machinery (the ZIP generator).

Self-containment uses a **static vcpkg triplet**, just like Linux/macOS, instead
of shipping a pile of DLLs:

- `x64-windows-static-md` — folds gRPC/protobuf/abseil into the executables and
  plugin DLLs but keeps the **dynamic CRT** (users need the near-universal Visual
  C++ Redistributable). The usual sweet spot.
- `x64-windows-static` — folds the CRT in as well: zero external dependency,
  larger binaries.

Either gives the Linux bundle's benefits: one artifact decoupled from system
library versions, with gRPC pinned at build time (good for the protocol
fingerprint story). The `x64-windows-static-md` overlay triplet
(`triplets/x64-windows-static-md.cmake`) sits alongside the four static
linux/osx triplets.

### Distribution channels

- **Scoop** — the best fit for a headless dev/CI tool: user-scoped, no admin, and
  a "bucket" is the exact analogue of the Homebrew tap (a small repo of JSON
  manifests pointing at the release ZIP, with autoupdate). A `scoop-beebium`
  bucket repo would mirror `homebrew-beebium`. Lead with this.
- **WinGet** — Microsoft's official manager, built into Windows 10/11, so the
  broadest reach. Submit a manifest to `microsoft/winget-pkgs` (PR-based) or
  self-host; automatable with `wingetcreate`. Second priority, for discoverability.
- **Chocolatey** — entrenched in enterprise CI but older and admin-oriented; not
  pursued.

### Code signing (Authenticode)

Unlike Linux, unsigned Windows executables trip SmartScreen/Defender on download —
the counterpart to macOS notarization. Since OV certificates without hardware
tokens were withdrawn in 2023, the current route is **Azure Trusted Signing**
(Microsoft's managed service, GA 2024): inexpensive, no hardware token,
SmartScreen-reputable, with a GitHub Actions integration. Eligibility currently
requires either a verified organisation or roughly three years of individual
identity history. Signing matters more for the GUI (end users download it) than
the server (devs/CI tolerate a warning), but is worth doing for both.

### GUI client (future)

The eventual Windows front end (the counterpart to the macOS app and the planned
`beebium-gui` cask) would ship as an **MSIX** package — the modern Windows app
format, with clean install/uninstall, sandboxing and auto-update — distributed
through the **Microsoft Store** (which handles signing, trust and updates) and/or
**WinGet**. A WiX Toolset v5 **MSI** would only be added if traditional
enterprise deployment (Intune/SCCM/Group Policy) is needed.

### How the ZIP is built and shipped

- `triplets/x64-windows-static-md.cmake` — static gRPC/protobuf/abseil, dynamic
  CRT. `dumpbin /dependents` (asserted in CI) confirms only system DLLs + the
  VC++ runtime + our own ABI DLLs, no gRPC/protobuf/abseil DLLs.
- `cmake --install` produces the right tree on Windows with no fixups: `bin/`
  (exes + ABI DLLs + `extensions/<name>/`), `share/beebium/{roms,presets}`.
- `packaging/windows/make-zip.ps1` installs to a staging prefix and zips `bin/` +
  `share/` (root-level) into `beebium-server-<version>-windows-x64.zip` (~30 MB;
  the `lib/` import libraries are omitted).
- `.github/workflows/windows-package.yml` runs all of the above on `windows-2022`
  (vcpkg static-md install with the NuGet binary cache, build with `--parallel 2`
  to fit the ~3 GB server-main compiles on a 16 GB runner, the `dumpbin`
  self-containment gate, then `make-zip.ps1`), uploads the ZIP, and — on a
  separate clean runner — extracts it and runs a server (the build-isolation
  smoke). It exposes `workflow_call`, so `release.yml` reuses it and attaches the
  ZIP to the draft.
- `packaging/scoop/beebium-server.json` — the canonical Scoop manifest (bin shims
  for the four servers, `checkver`/`autoupdate`; `url`/`hash` are placeholders
  pinned at release time, like the Homebrew `sha256`).

### Remaining (Windows)

The `scoop-beebium` bucket (the Scoop analogue of the Homebrew tap) is **live and
pinned to `v0.1.0`** (`packaging/scoop/sync-bucket.sh`), so `scoop install
beebium-server` works. What remains:

1. A WinGet manifest.
2. Azure Trusted Signing (decide whether to sign from the next release or later).
3. Later: arm64 Windows (Windows on ARM), and the GUI MSIX + Store path.

The `static-md` vs full `static` decision is settled in favour of `static-md`
(dynamic CRT, only the VC++ Redistributable needed). The sign-now vs
ship-unsigned-first decision is still open.

## Install layout

`cmake --install` produces one relocatable `bin`/`lib`/`share` tree; the package
formats differ only in *where* they place it (see
[Package formats](#package-formats)). The **`.deb`** installs it under
`/opt/beebium` with `/usr/bin` symlinks onto the four servers so they are on
`PATH`:

```
/opt/beebium/
├── bin/
│   ├── beebium-model-b, beebium-model-b-plus, beebium-model-b-plus-128k, beebium-model-b-romram
│   └── extensions/<name>/{<plugin>.so, manifest.json}
├── lib/{libbeebium_extension_api.so, libbeebium_extension_ui_proto.so}
└── share/beebium/{roms,presets}/
/usr/bin/beebium-model-b -> /opt/beebium/bin/beebium-model-b   (+ the other three)
```

The **`.tar.gz`** ships the same `bin`/`lib`/`share` tree inside a single
relocatable directory (`beebium-server-<version>-linux-<arch>/`) that the user
extracts anywhere and adds the directory's `bin/` to `PATH`. The macOS keg and
the Windows `.zip` use the same shape. In every case the binaries resolve their
extension ABI libraries, plugins, ROMs and presets relative to their own on-disk
location (read from the OS, not `argv[0]`), so the symlinks work and the tree
relocates intact. See [Deployment](deployment.md) for the discovery details.

## Package formats

`cmake --install` produces the tree; CPack (`cmake/BeebiumPackaging.cmake`)
produces the two **Linux** distributable packages below. (The macOS keg is built
by Homebrew from the formula; the Windows `.zip` is assembled by
`packaging/windows/make-zip.ps1`, not CPack — see the [macOS](#the-macos-homebrew-formula)
and [Windows](#windows) sections.)

- **`.deb`** (`beebium-server_<version>_<arch>.deb`) — for Debian / Ubuntu /
  Raspberry Pi OS. Runtime dependencies are derived with `dpkg-shlibdeps`; since
  gRPC/protobuf are static, it depends only on `libc6 (>= 2.36)`, `libgcc-s1`,
  `libstdc++6`. The maintainer scripts create and remove the `/usr/bin` symlinks.
- **`.rpm`** (`beebium-server-<version>-1.<arch>.rpm`, arch `x86_64` /
  `aarch64`) — the RPM sibling of the `.deb`, for the **Fedora / RHEL family**
  (and openSUSE): `sudo dnf install ./<file>.rpm`. It lays the same `/opt/beebium`
  system tree with the same `/usr/bin` symlinks, created and removed by `%post`/
  `%preun` scriptlets. RPM's `find-requires` derives the base-library
  dependencies (and the versioned glibc floor, `libc.so.6(GLIBC_2.36)`)
  automatically from the ELF binaries, so no dependencies are declared by hand.
  Built by the same bundle image (which carries `rpmbuild`); see
  [linux-rpm-packaging.md](plans/linux-rpm-packaging.md) for the design and the
  planned Copr (`dnf install`) channel. The `.deb`/`.rpm` scriptlets differ only
  in their argument semantics (a `.deb` switches on an action word; an RPM gets
  an instance count).
- **`.tar.gz`** (`beebium-server-<version>-linux-<arch>.tar.gz`) — a
  **relocatable** bundle for distros with no native package here (Arch, NixOS)
  and bare containers. It unpacks to a single self-named directory
  (`beebium-server-<version>-linux-<arch>/`); extract it anywhere and put that
  directory's `bin/` on `PATH` (no root, no `tar -C /`). The system packages and
  the `.tar.gz` diverge here on purpose (`CPACK_PROJECT_CONFIG_FILE` =
  `cmake/BeebiumCPackOptions.cmake`): the `.deb`/`.rpm` are `/opt/beebium` system
  packages, the `.tar.gz` is a user-relocatable directory. (Or use the AUR
  package, when available.)

### ROMs

The full `roms/` tree is shipped in every package. The copyright holders for the
ROMs Beebium needs are all commercially defunct, so the enforcement risk is
treated as negligible; bundling them means there is no ROM-sourcing step in the
user's quickstart. Disc images are **not** shipped — the disc under test is the
user's own BBC software.

## Building the Linux bundle

(The macOS formula builds via `packaging/homebrew/test-formula.sh` /
`windows-package.yml` builds the Windows `.zip`; see those sections. This section
covers the Linux static bundle.)

`docker/linux-bundle/Dockerfile` builds the bundle for one architecture. On an
Apple Silicon (arm64) Mac, the `arm64` build is **native** (fast) and the
`amd64` build runs under emulation (slow):

```bash
docker buildx build --platform linux/arm64 \
    -f docker/linux-bundle/Dockerfile \
    --build-arg VCPKG_TRIPLET=arm64-linux-static \
    --target artifact --output type=local,dest=./_artifacts .
# amd64: --platform linux/amd64 --build-arg VCPKG_TRIPLET=x64-linux-static
```

Notes:

- vcpkg is pinned to the same commit the macOS CI uses, so all artifacts link
  identical gRPC/protobuf versions. Its dependency install is a separate layer
  keyed only on `vcpkg.json` + the static triplet overlays
  (`triplets/{arm64,x64}-linux-static.cmake`), with a binary-cache mount, so
  source-only changes do not re-trigger the ~30-40 min gRPC build.
- The server-main translation units instantiate the full `Machine<Hardware>`
  template against the static gRPC/protobuf headers and peak at ~2.5-3 GB each.
  A depth-1 Ninja job pool (`beebium_heavy_compile`) serialises just those, and
  the Dockerfile's `BUILD_JOBS` arg caps overall parallelism, so the build does
  not OOM on a memory-limited host (e.g. an 8 GB Docker VM).

## Persistent dependency caching — the build-env image

The expensive part of the Linux build is the vcpkg dependency install (the
static gRPC/protobuf/abseil/openssl/… stack, ~30–40 min native and far longer
emulated). Everything downstream — the server compile, packaging, smoke — is
minutes. So the goal is to build those dependencies **once per input change** and
reuse them, durably, across the multi-month gaps between releases.

The two caches the build previously leaned on are both non-durable on ephemeral
runners, which is why a release after any quiet period was a full from-scratch
build (~2 h):

- `--cache-from/--cache-to type=gha` (buildx layer cache) is evicted after 7
  days unused and capped at ~10 GB per repo; the last release before a gap
  finds it cold.
- the Dockerfile's `--mount=type=cache,target=/root/.cache/vcpkg` is
  **runner-local**, so it is always empty on a fresh hosted runner.

### The design

A per-architecture **builder base image**,
`ghcr.io/rob-smallshire/beebium-build-env:<tag>`, holds the toolchain, the
pinned vcpkg, and the fully-installed static dependency tree
(`/src/vcpkg_installed`). The release build starts *from* it, so the dependency
install never runs at release time.

- **Content-addressed tag.** `<tag>` is a hash of exactly the inputs that
  determine the dependency tree: `vcpkg.json`, the static triplet overlays under
  `triplets/`, the pinned vcpkg commit, and the Dockerfile's builder stage up to
  (and including) the `vcpkg install`. Any change to those inputs yields a new
  tag; nothing else does. Each architecture gets its own image (the tag carries
  the arch), keeping the arm64-native / amd64-emulated buildx layout unchanged.
- **Built only when missing.** `.github/workflows/build-env.yml` computes the
  tag, checks whether that image already exists in GHCR, and builds + pushes it
  only when it does not (plus a manual `workflow_dispatch`). It pushes with the
  workflow `GITHUB_TOKEN` (`packages: write`, already granted in `release.yml`).
  So the ~40-min dependency build happens once per input change, not once per
  release.
- **Reused with a from-scratch fallback.** The release Dockerfile's builder
  stage begins `FROM ${BUILDENV_IMAGE}`, where `BUILDENV_IMAGE` is a build-arg
  that **defaults to a locally-built stage** carrying the same dependency
  install. When the release workflow passes the GHCR tag, buildx pulls the
  prebuilt image and the local dependency stage is pruned as dead code — the
  install is skipped entirely. When no image is supplied (a fresh fork, or a
  plain local `docker buildx build`), the default builds the dependencies from
  scratch, so the checkout still builds with no registry access.

### Why this is durable where the runner caches are not

A GHCR image is content-addressed and permanent (it is not subject to the 7-day
Actions-cache eviction), and the tag rotates automatically only when the
dependency inputs change. Crucially, the Linux dependencies are built **inside
the pinned `debian:bookworm` build floor**, so their ABI is fixed by that image,
not by the mutable hosted-runner image. This is the difference from the Windows
NuGet binary cache, whose keys ride the hosted `windows-2022` toolchain: there a
runner-image bump silently moves the vcpkg ABI hash and invalidates the cache
even when the compiler version string is unchanged. Building Linux in a pinned
container sidesteps that entirely, which is what makes a long-lived, content-
addressed build-env image safe here.

## Validation

Packaging is validated as a ladder of increasing fidelity, ending at the
principle that matters most: **test the artifact you ship, at the point you ship
it.** A green *build* proves the bundles are good; it does not prove the publish
path shipped the right bytes — so the last two layers validate the actual
published assets.

1. **`install_smoke`** (CTest, labelled `packaging`) — installs to a staging
   prefix and boots each server, asserting the ABI libs resolve via RPATH, the
   plugins are discovered, ROMs resolve, and the gRPC server comes up.
2. **`scripts/smoke-installed-tree.sh`** — the reusable primitive: validates an
   *extracted* bundle at a given prefix on any OS: `ldd` self-containment (no
   `libgrpc`/`libprotobuf`/…), plugin discovery, ROM discovery + boot, and a
   bare-command (PATH-symlink) check. Prefix-agnostic (canonicalised to absolute).
3. **Build-isolation smoke** (CI, a *clean* runner separate from the build,
   against the *just-built* artifact) — proves the artifact installs and runs
   without the build environment. Linux `smoke-deb` (install the `.deb` in a
   fresh `debian:bookworm`, plus the Python-client interaction test),
   `smoke-rpm` (`dnf install` the `.rpm` in a fresh `fedora:latest`, same
   interaction test) and `smoke-tarball` (extract the relocatable `.tar.gz` in a
   fresh Arch); Windows `smoke` (extract the `.zip` on a fresh runner and run a
   server). macOS is inherently covered because `brew install` *is* the build +
   install.
4. **Publish-boundary gate** (the `Verify the publishable artifacts` step in
   `release.yml`, before the upload) — validates the *exact set of files about to
   be attached*: exactly the seven expected filenames (no missing/extra/dupes),
   each Linux `.tar.gz` is a single relocatable top-level directory (not `/opt`),
   each `.deb`'s `Architecture` and each `.rpm`'s arch match their filenames, and
   the shipped amd64 server actually runs (`smoke-installed-tree.sh`). If any
   check fails the job stops and no draft is created with bad assets. This closes
   the gap that once published stale `/opt`-rooted tarballs while every smoke job
   stayed green.
5. **Published end-to-end smoke** (`release-smoke.yml`, `workflow_dispatch` with a
   `version` input, run **after** publishing + syncing the tap/bucket) — installs
   from the real public channels on clean per-platform runners: Linux downloads
   the `.deb`/`.tar.gz` from the Release URL; macOS `brew install beebium-server`
   from the tap; Windows `scoop install beebium-server` from the bucket. This is
   the only layer that exercises the actual download URLs, hashes and
   package-manager plumbing, so it must run post-publish (they do not exist
   before).

## CI

`.github/workflows/linux-packages.yml` runs on `workflow_dispatch` (the
from-source static gRPC build is too expensive for per-push runs) and on
`workflow_call` (the release flow reuses it). It builds both architectures on
native runners (`ubuntu-latest` for `amd64`, `ubuntu-24.04-arm` for `arm64`, free
on public repos), then exercises the produced packages in clean `debian:bookworm`
(`.deb`, with the Python interaction smoke), `fedora:latest` (`.rpm` via `dnf`,
same interaction smoke) and Arch (`archlinux` for x86_64, Arch Linux ARM for
arm64) containers. Each arch's build uploads **only its own arch's files** — two
globs, `_artifacts/*<arch>*` for the Debian-token names and
`_artifacts/*<rpm_arch>*` for the RPM's `x86_64`/`aarch64` name: the buildx cache
can leave stale cross-arch files in the output dir, and without the filter the
release job's artifact merge would collide same-named assets and publish the
wrong ones.

`.github/workflows/macos-package.yml` builds, installs, tests and audits the
Homebrew formula on `macos-14` (arm64) by running
`packaging/homebrew/test-formula.sh`. The source build is cheap (~1 min), so it
runs on PRs that touch the formula or the build system, plus on demand. It is
**arm64 only**: GitHub's Intel `macos-13` runners are scarce and deprecated, and
a release-gating Intel leg stuck in the runner queue would block the draft
indefinitely.

`.github/workflows/macos-intel.yml` covers Intel (`x86_64`) **best-effort** — the
same `test-formula.sh`, on `macos-13`, triggered weekly / on demand / on
formula-or-build PRs. It is standalone and **never gates a release**, so a stuck
or absent Intel runner only affects that run. The formula is a source build, so
Intel Macs work for users regardless of CI. For reliable Intel coverage, register
an Intel Mac as a self-hosted runner and point `runs-on` at its label.

`.github/workflows/windows-package.yml` builds the static-md `.zip` on
`windows-2022`, runs the `dumpbin` self-containment gate, and — on a separate
clean runner — the build-isolation smoke (see the [Windows](#windows) section).
It also exposes `workflow_call`.

`.github/workflows/release.yml` ties these together: pushing a `v*` tag (the
final step of the `bump-my-version` release) reuses `linux-packages.yml`,
`macos-package.yml` and `windows-package.yml` (via `workflow_call`) to build and
smoke every package, then the `release` job runs the **publish-boundary gate**
(the `Verify the publishable artifacts` step — see [Validation](#validation)) and
only if it passes creates a **draft** GitHub Release with the Linux `.deb`/
`.tar.gz` and the Windows `.zip` attached. The draft is published manually; the
Homebrew tap and Scoop bucket are then synced (`packaging/homebrew/sync-tap.sh`
and `packaging/scoop/sync-bucket.sh`), after which `release-smoke.yml` verifies
the live public install paths. Note the Scoop manifest points at the `.zip`
**release asset**, so `sync-bucket.sh` requires the Release to be *published*
first (unlike the tap, which builds from the source archive available for any
tag).

## Status

**Done:**
- **Linux:** self-contained static bundle for `amd64` and `arm64`; `.deb`
  (`/opt/beebium`, correct per-arch `Architecture`, minimal deps, `/usr/bin`
  symlinks), `.rpm` (the same tree for the Fedora / RHEL family, `%post`/`%preun`
  symlink scriptlets, arch derived by RPM `find-requires`) and **relocatable**
  `.tar.gz`, all validated by install-in-clean-distro on Debian, Fedora and Arch
  (x86_64 + Arch Linux ARM).
- **macOS:** Homebrew formula (`beebium-server`), source build against Homebrew's
  grpc/protobuf, validated in CI on arm64 (Intel best-effort via
  `macos-intel.yml`). The tap `rob-smallshire/homebrew-beebium` is live and synced
  to `v0.1.0`; the stable `brew install beebium-server` works and is validated
  end-to-end by the `release-smoke.yml` macOS leg.
- **Windows:** self-contained `x64-windows-static-md` `.zip`, built + smoke-tested
  in CI (`windows-package.yml`) and attached to the release. The Scoop bucket
  `rob-smallshire/scoop-beebium` is live and pinned to `v0.1.0`, so
  `scoop install beebium-server` works (validated by `release-smoke.yml`).
- **Release pipeline:** `release.yml` builds all three platforms on a `v*` tag,
  runs the publish-boundary verification gate (now the **seven-file** set,
  including the two `.rpm`s), and produces a **draft** GitHub Release. The first
  release (`v0.1.0`) has been cut and **published**, with all channels live
  (Release `.deb`/`.tar.gz`/`.zip`, Homebrew tap, Scoop bucket) and re-verified by
  `release-smoke.yml`. `v0.1.0` **predates the `.rpm`**, which attaches from the
  next tagged release.

**Remaining (macOS Homebrew):**
- Wire the tap's bottle-building workflow so installs pour a pre-built binary
  instead of compiling from source (matters at CI scale; availability is done).
- Possibly add a Homebrew-free self-contained macOS `.tar.gz` for macOS CI
  (needs the vcpkg-static build path).

**Remaining (Windows):**
- WinGet manifest; Authenticode signing (Azure Trusted Signing); later arm64
  Windows and the GUI MSIX + Store path.

**Remaining (other):**
- Cut the next tagged release so the Linux `.rpm`s attach to a published Release,
  then extend `release-smoke.yml` with a Fedora `dnf`-install leg.
- An AUR `beebium-bin` PKGBUILD that repackages the `.tar.gz` for Arch.
- A **Copr** project (`dnf copr enable rob-smallshire/beebium && dnf install
  beebium-server`) — the Fedora analogue of the Homebrew tap and Scoop bucket,
  taking the `.rpm` built here as its input; and a Fedora leg in
  `release-smoke.yml` once that channel is live. See
  [linux-rpm-packaging.md](plans/linux-rpm-packaging.md).
- Publishing the Python client to PyPI and the TypeScript client to npm.
- On-real-hardware validation on a 64-bit Raspberry Pi 4 / 400.
