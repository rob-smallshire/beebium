# Shipping the emulator on PyPI: `beebium-server` platform wheels

Status: design, August 2026. Follows section 9 of
`python-client-architecture.md`, which recorded the idea; this document settles
the shape. Nothing here is implemented yet.

## 1. Goal

In a fresh Python environment:

```
pip install beebium beebium-server
```

gives a working headless system: the Python client can launch a Beebium server
from the installed package, with the ROMs, presets and extensions it needs, and
drive it over gRPC. Nothing else is installed, no environment variables are
set, and no ROM files are located by hand.

At the same time the client must keep working with servers installed by any
other means -- a Homebrew formula, a `.deb`, a Scoop shim, a checkout build, or
a server already running somewhere on the network -- and must never silently
prefer the wrong one.

## 2. Constraints that shape the design

Measured on the v0.1.3 release artifacts.

- **Size.** The installed tree is ~131 MB per platform uncompressed: four
  near-identical static server binaries of ~29 MB each (`beebium-model-b`,
  `-plus`, `-plus-128k`, `-romram`), ~17 MB of extension plugins, ~4 MB of
  shared ABI libraries, and ~300 KB of ROMs and presets. Compressed it is
  ~48 MB (Linux) / ~29 MB (Windows). PyPI's default limit is 100 MB per file
  and 10 GB per project, so this fits today without asking for a raise, but
  every release spends ~5 x 48 MB of the project quota.
- **The server is self-locating.** `RomPaths`, `PresetPaths` and extension
  discovery resolve relative to the executable's own directory
  (`../share/beebium/{roms,presets}`, `<exe_dir>/extensions`), the shared
  libraries are found through `$ORIGIN/../lib` (`@loader_path` on macOS), and
  `BEEBIUM_ROM_DIR` / `--rom-dir` override. A wheel that preserves the
  installed `bin/ lib/ share/` layout therefore needs **no server-side
  change**: the binaries do not know they are inside a Python package.
- **The server has a default MOS.** `--mos <filepath>` is optional on the
  server (`Memory::DEFAULT_MOS_ROM`, resolved in the ROM directory). The
  Python client, however, makes `mos_filepath` a required argument of
  `Beebium.launch()` and `ServerProcess`, so today a user cannot launch without
  first finding a ROM file. That is a client gap, not a server one.
- **Compatibility is the protocol fingerprint, not the version.** A client only
  talks to a server whose compiled-in protocol fingerprint matches its own
  (`ProtocolMismatchError` at connect). Whatever the wheel ranks at in the
  server search, a wrong pairing still fails loudly; the search order decides
  *which* server is tried first, not whether an incompatible one can be used.
- **macOS has no binary bundle today.** Linux ships `.deb`/`.rpm`/`.tar.gz`
  and Windows a `.zip`, all self-contained (static gRPC/protobuf). macOS is
  delivered only as a Homebrew formula that builds from source. macOS wheels
  need a new relocatable bundle artifact.
- **Lockstep versioning.** One monorepo version covers server and clients; a
  release is one tag. `beebium-server` wheels are versioned by that tag like
  everything else.

## 3. The distribution

| | |
|---|---|
| PyPI name | `beebium-server` (to be claimed with a pending trusted publisher, as `beebium` was) |
| Import package | `beebium.server` -- a regular package inside the existing PEP 420 `beebium` namespace |
| Wheel tags | `py3-none-<platform>` -- pure-Python shim plus platform binaries; no CPython-version coupling, one wheel per OS/arch |
| Platforms | `manylinux_2_36_x86_64`, `manylinux_2_36_aarch64` (PEP 600 tag for the Debian bookworm glibc floor the bundles are already built against), `macosx_11_0_arm64`, `macosx_10_15_x86_64` (or whatever floor the macOS bundle build sets), `win_amd64` |
| sdist | none -- there is no source that `pip` could build; PyPI accepts wheel-only projects |
| Dependencies | none at runtime. It does not depend on `beebium` and `beebium` does not depend on it |
| Version | the monorepo version, from the same `bump-my-version` run as everything else |

The wheel is a **repackaging of the existing release bundles**, not a new
compile: the build step takes the `.tar.gz` / `.zip` (and, once it exists, the
macOS equivalent) that `release.yml` already produces and validates, and lays
them into a wheel. Server bytes on PyPI are therefore byte-identical to the
ones on GitHub, Homebrew (for the bundle-based path) and Scoop, and have passed
the same install-smoke layers.

### 3.1 Layout inside the wheel

```
beebium/server/
    __init__.py          # version, paths API, launch helpers (section 5)
    _bundle/             # the installed tree, layout preserved verbatim
        bin/
            beebium-model-b[.exe]
            beebium-model-b-plus[.exe]
            beebium-model-b-plus-128k[.exe]
            beebium-model-b-romram[.exe]
            extensions/<name>/{<plugin>, manifest.json}
            *.dll                         # Windows only: plugins sit beside the exe
        lib/                              # Linux/macOS: extension ABI libraries
        share/beebium/{roms,presets}/
beebium_server-<ver>.dist-info/
    licenses/COPYING.txt
```

Keeping `_bundle/` identical to the tarball means the server's own relative
discovery and RPATH work untouched, and the smoke script
`scripts/smoke-installed-tree.sh` can validate an unpacked wheel exactly as it
validates a tarball.

### 3.2 Executable permission

Wheels are zip files; zip entries carry Unix mode bits and `pip` preserves
them on install, so the binaries arrive executable on Linux/macOS. As belt and
braces the `executable_filepath()` accessor (section 5) checks `os.access(...,
X_OK)` and, if a file is not executable, `chmod`s it before returning -- a
one-time, idempotent repair for installers that drop the bits.

### 3.3 Console scripts

The wheel registers the four server names as console-script entry points
(`beebium-model-b = beebium.server._launch:model_b`, etc.). Each is a thin
`os.execv` of the bundled binary. This gives `pip install beebium-server`
**PATH parity** with the `.deb` symlinks, the Homebrew `bin/` links and the
Scoop shims: a user (or the TypeScript client, or a shell script) can type
`beebium-model-b` and get the wheel's server, exactly as with any other
install. Because it is an `exec`, not a wrapper process, signals, exit codes
and stdout (which the clients parse for "Listening on port N") behave as for
the bare binary.

On Windows the entry-point launcher is a small `.exe` generated by `pip`; it
spawns rather than execs, so the Python client should not rely on the shim for
signal delivery and must prefer the real binary path (which section 5 gives
it).

## 4. Building the wheels

A new `packaging/python-server/` holds a small uv project:

```
packaging/python-server/
    pyproject.toml          # hatchling; name beebium-server; version dynamic
    src/beebium/server/     # __init__.py, _launch.py, _paths.py, py.typed
    build_wheel.py          # <bundle-archive> <platform-tag> -> dist/*.whl
    COPYING.txt             # copy of the repository licence (as the client does)
```

`build_wheel.py` unpacks a bundle archive into a scratch `src/beebium/server/
_bundle/`, runs `uv build --wheel`, and renames/retags the result to the
requested platform tag (hatchling is told the wheel is not pure Python via a
build hook that sets `build_data["pure_python"] = False` and
`build_data["tag"]`; if that proves awkward, retagging with the `wheel` tool's
`tags` command is the fallback). The bundle contents never live in the
repository; the directory is gitignored and produced only at build time.

Verification follows the client's wheel-test discipline (design doc section
4.3): every wheel is installed into a **fresh venv on a runner of the matching
platform**, then

1. `python -c "import beebium.server; print(beebium.server.executable_filepath())"`
2. `bash scripts/smoke-installed-tree.sh <site-packages>/beebium/server/_bundle`
   (self-containment, extension discovery, ROM discovery, boot)
3. the Python client launches a server from the wheel with no arguments and
   reads the boot banner (`packaging/smoke/test_smoke.py` is reused, with
   `BEEBIUM_SERVER` unset so the resolver has to find the wheel)

in CI before anything is published.

### 4.1 Where it runs

`release.yml` gains a `server-wheels` job that `needs` the three bundle
workflows (it is the one Python job that genuinely consumes their outputs --
unlike the client publish, which stays independent and fast), downloads the
bundle artifacts, builds all wheels on one Linux runner, uploads them as an
artifact, then fans out the per-platform verification matrix above. Publishing
lives in its own reusable workflow, `publish-server-wheels.yml` (called from
`release.yml`, dispatchable by hand for TestPyPI), uploading with `uv publish
--trusted-publishing always` in the `pypi` environment -- the same pattern as
`publish-python.yml`. PyPI matches a trusted publisher on the *reusable*
workflow's filename, so the `beebium-server` pending publisher is registered
as: owner `rob-smallshire`, repository `beebium`, workflow
`publish-server-wheels.yml`, environment `pypi`. Until macOS bundles exist, the job publishes
the Linux and Windows wheels and skips macOS with an explicit log line -- never
silently.

### 4.2 The macOS bundle

New job in `macos-package.yml` (arm64 first; x86_64 when the Intel runner is
reliable): the existing vcpkg static build + `cmake --install` + `cpack -G
TGZ`, exactly the Linux bundle recipe, producing
`beebium-server-<ver>-macos-<arch>.tar.gz`. Binaries and plugins get an ad-hoc
code signature (`codesign -s -`) so arm64 macOS will execute them. Binaries
installed by `pip` carry no quarantine attribute, so Gatekeeper/notarisation
does not apply; this must be confirmed on a real machine as part of the
verification matrix, and if it fails the fallback is notarising the tarball,
which is a separate piece of infrastructure (Apple developer credentials in
CI). The tarball is also attached to the GitHub Release, so macOS gains a
no-Homebrew install path as a side effect.

## 5. The Python API

### 5.1 `beebium.server` (the wheel's own package)

Small, dependency-free, and usable without the client:

```python
import beebium.server

beebium.server.__version__            # "0.1.4"
beebium.server.bundle_dirpath()       # Path to _bundle/
beebium.server.executable_filepath(variant="model-b")   # ensures executable
beebium.server.rom_dirpath()          # _bundle/share/beebium/roms
beebium.server.preset_dirpath()
beebium.server.variants()             # ("model-b", "model-b-plus", "model-b-plus-128k", "model-b-romram")
```

`variant` is the suffix of the binary name; `"model-b"` is the default
everywhere, matching the client.

### 5.2 `beebium.client` (the existing client)

Choosing a server is a first-class, explicit choice, not only a search order.
A server is never just one binary: an *installation* is the four machine
binaries plus the extensions, ABI libraries, ROMs and presets laid out beside
them -- which is what a `.deb`, a Homebrew keg, a Scoop app, a checkout build
and the wheel each provide. The client therefore gains one small value type:

```python
from beebium.client import ServerInstallation

ServerInstallation.from_executable(path)   # one binary; siblings found beside it
ServerInstallation.from_root(prefix)       # an install root containing bin/ (or the bin/ dir itself)
ServerInstallation.installed_wheel()       # the beebium-server wheel, if importable
ServerInstallation.on_path()               # whatever `beebium-model-b` resolves to on PATH
ServerInstallation.default()               # the resolution below

installation.executable_filepath(variant="model-b")
installation.variants()                    # which of the four are present
installation.rom_dirpath / preset_dirpath  # may be None for a bare binary
installation.describe()                    # origin + path, for logs and errors
```

`Beebium.launch()` and `ServerProcess` take `server=` accepting any of: a
`ServerInstallation`; a path to a binary (today's `server_filepath`, kept as a
deprecated alias); or a path to an install root. A `variant=` parameter
selects the machine within the installation (`"model-b"` default). When the
user is specific, that is what runs -- no fallback, and a missing binary or
variant is an error naming the installation, not a silent slide down the list.

Only when the user is not specific does resolution apply, in this order:

1. `BEEBIUM_SERVER` -- a binary path or an install root (an invalid value
   raises, as today)
2. a build directory in the surrounding checkout (development only)
3. **the installed `beebium.server` wheel**, found by importing
   `beebium.server` (`ImportError` means not installed; the client never
   hard-depends on it)
4. `beebium-model-b` on `PATH`

The wheel is the default because it is the one installation version-locked to
the client by construction (same tag, same fingerprint); `PATH` holds whatever
the machine happens to have. `Beebium.connect()` -- attaching to a server
started by any other means -- is untouched. If the wheel and client versions
differ, the client logs a warning naming both and still tries the wheel; the
fingerprint handshake remains the real guard. `tests/test_server_discovery.py`
pins every explicit form and the default order.

Two further small changes:

- **`mos_filepath` becomes optional** in `ServerProcess` and
  `Beebium.launch()`. When `None`, `--mos` is not passed and the server
  resolves its default MOS from its own ROM directory, so `Beebium.launch()`
  with no arguments is a complete call. `basic_filepath` is already optional
  and follows the same rule.
- The pytest plugin's `--beebium-server` option accepts the same forms as
  `server=`, and its ROM fixture tries `beebium.server.rom_dirpath()` after the
  checkout `roms/` and before the home/`/usr/share` locations.

### 5.3 The `beebium[server]` extra (deferred)

The intended one-line form is an extra pinning the server wheel to the **same
version**:

```toml
[project.optional-dependencies]
server = ["beebium-server==0.1.5"]   # kept in lockstep by bump-my-version
```

so `pip install "beebium[server]"` gives the complete headless system and the
exact pin makes version skew impossible on that path.

**This is deferred.** It was tried (and reverted) because an exact pin on a
sibling published *later in the same release* cannot resolve during the release
window. `bump-my-version` moves the pin in the bump commit, but `beebium-server`
`<new>` only reaches PyPI ~45 minutes later (after the Linux bundles build), and
the bump cannot update `uv.lock` (its `beebium-server` entry carries
version-specific wheel URLs and hashes that a regex cannot rewrite; only
`uv lock`, run *after* the version publishes, can). `uv` performs a **universal
resolution that includes every extra** on any non-`--frozen` `uv run`,
`uv sync`, `uv export` or `uv lock` — so throughout that window they all fail
("no version of `beebium-server==<new>`"), *even though the extra is never
installed*. That breaks the client's own publish (`publish-python.yml` runs
`test-wheel.sh`, which runs `uv export`) and every developer's `uv sync` on the
bump commit.

Options considered:

- **`--frozen` everywhere** — make CI and developers use the lock as-is so the
  extra is never re-resolved. Works, but it must become the default for *every*
  `uv` invocation (a plain `uv sync`/`uv run` would fail for every developer on
  every bump commit), permanently weakens lock checking, and leaves `uv.lock` a
  release behind the pin. More friction than the one-line form is worth.
- **A separately published metapackage** (e.g. `beebium-headless`) whose pins
  are only ever published *after* both `beebium` and `beebium-server` exist —
  keeps exact pins without the in-repo lock circularity, at the cost of a third
  distribution and its own release step.
- **A loose pin** (`beebium-server~=0.1`) — resolves any time, but loses the
  exact-version lockstep the extra exists to provide.

None is worth blocking on. The **documented and supported path is the two-package
form `pip install beebium beebium-server`** (section 1). The client already
defaults to the installed wheel (section 5.2), so the user experience is nearly
identical. Revisit if the metapackage or a post-publish `uv lock` step in the
release runbook later proves worthwhile.

## 6. Size, now and later

Five wheels x ~48 MB is acceptable today. Two independent reductions are
available later and are deliberately **not** part of this design:

- **One binary, four machines.** The four servers are the same code with a
  different `Machine<Hardware>` instantiation; a single `beebium-server
  --machine model-b-plus` executable would remove ~85 MB uncompressed per
  platform (and simplify every package). That is a server-side refactor with
  its own compatibility story for the existing per-binary install layouts and
  client lookups, so it stands alone.
- **Strip and compress** the static binaries more aggressively (they are
  already `-s`-stripped release builds; UPX-style packing is not worth its
  Gatekeeper/AV false-positive risk).

If the quota ever matters, PyPI grants per-project limit increases on request.

## 7. Phases

1. **Claim the name.** Register the `beebium-server` pending publisher against
   `release.yml`/environment `pypi`. The name is only held once a release
   uploads (section 4.1), so this phase is the publisher plus phase 2.
2. **Linux + Windows wheels.** `packaging/python-server/`, `build_wheel.py`,
   the `server-wheels` and verification jobs, publish. Client changes from
   section 5.2 (optional `mos_filepath`, wheel resolution step, `variant`),
   with tests against the installed wheel. Ships `pip install beebium
   beebium-server` on Linux and Windows.
3. **macOS bundle + wheels.** The `cpack TGZ` job with ad-hoc signing, the
   on-runner verification, then the two macOS wheels join the publish.
4. **`beebium[server]` extra and docs.** The pinned extra, README/quickstart
   in both packages, `docs/packaging.md` and `deployment.md` updated.

Each phase is shippable on its own; a release after phase 2 simply has no
macOS wheel and says so in the job log.

## 8. Decisions deferred to the maintainer

- (Decided: ship the `test-scratch-ram` extension for now, as the bundles do.)
- (Decided: the `beebium[server]` pinned extra is wanted.)
- Whether to take on notarisation for macOS if ad-hoc signing turns out not
  to be enough for pip-installed binaries on a given macOS version.
- Whether, and when, to pursue the single-binary server (section 6).
