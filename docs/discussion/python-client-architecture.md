# Python Client Architecture: Completeness, Extensibility, Packaging

Status: proposal (branch `python-client`)
Author: design note, July 2026

This note plans the evolution of `clients/beebium-python-client` against five goals:

1. **Completeness** -- cover enough of the server's gRPC surface.
2. **Extensibility** -- mirror Beebium's peripheral-extension model in the Python API.
3. **Organisation** -- a `beebium` namespace package with separately-deployable sub-packages.
4. **Packaging** -- build wheels.
5. **Deployment** -- publish to PyPI.

The extensibility model is drawn from two existing Stevedore-based
architectures the maintainer already runs: `sixty-north/asyoulikeit`
(the generic `Extension`/factory core) and
`sixty-north/demonstrable-visning` (the PEP 420 namespace + separately
deployed sibling distributions). This design copies their shape.

---

## 1. Current state

The client is a single flat distribution `beebium` (hatchling, `src/beebium`),
with per-service modules and generated stubs committed under
`beebium/_proto`. It is not yet on PyPI (the name is unclaimed).

### 1.1 Service coverage

Covered (module + wired stub): `DebuggerControl`, `ParasiteDebuggerControl`,
`DeviceInspection` (cpu/memory/via/crtc/video_ula/latch/sound/tube_ula),
`KeyboardService`, `VideoService`, `SystemService`, `DiscService`,
`EconetService`, `EconetTransportService`, `SerialService`,
`IndicatorService`, `SidewaysService`, `TubeService`, plus the extension
surfaces `ExtensionRpc` and `ExtensionUiService`.

Gaps:

- **`PeripheralExtensionService` -- not covered, not even generated.** This is
  the *core, always-present* service that enumerates loaded extensions
  (`name`, `id`, `config`, `parameters` schema, `storage_devices`, `has_ui`).
  It is the reflective capability that the whole extensibility goal depends on.
- **`AudioService` -- uncovered.** `audio.proto` (`SubscribeAudio`,
  `GetAudioFormat`) has a generated `_grpc` stub but is never wired into
  `Connection` and has no client module. (`sound.py` is SN76489 *device
  inspection*, not the audio sample stream.)
- **`acorn_rtc` / `scsi_host_adapter`** -- pb2 stubs are committed but have no
  client module: dangling generated code.

### 1.2 Proto-generation drift

`scripts/generate_proto.sh` does **not** generate `tube`, `acorn_rtc`,
`scsi_host_adapter`, `peripheral_extension`, or `audio` wiring, yet
`tube_pb2`, `acorn_rtc_pb2`, `scsi_host_adapter_pb2` are committed. The script
and the committed tree have diverged; re-running the script yields an
inconsistent result. This must be repaired before any restructure.

### 1.3 Extensibility today

Extension clients (`Aun`, `Piconet`, `HostSerial`, `RpcSerial`) all share one
shape -- wrap an `ExtensionChannel`, know a logical service name (e.g.
`"AunService"`, `"HostSerial"`), own their pb2 module -- but they are
**hardcoded as properties on `Beebium`** (`client.py`). There is no
registration seam, so a third-party extension's Python client cannot be
shipped separately and discovered. That is precisely what this design fixes.

---

## 2. Two notions of "extension"

The design hinges on separating two things the word "extension" conflates:

- **Server-loaded extension instances** -- what a *running server* has loaded.
  Discovered per-connection at runtime via
  `PeripheralExtensionService.ListExtensions`. Dynamic, keyed by instance `id`,
  typed by manifest `name` (kebab-case: `aun`, `piconet`, `host-serial`,
  `rpc-serial`, `acorn-scsi`, `acorn-rtc`, ...).

- **Client-side extension adapters** -- installed Python plugins that know how
  to *talk to* a given extension type: its logical RPC service name, its pb2
  messages, and a typed façade. Static, installed via entry points.

The client's job is to **bridge** these: read what the server actually loaded,
then for each loaded extension look up a matching installed adapter and bind it
to the `ExtensionChannel` + `extension_id`. An extension with no installed
adapter still appears in the listing (with its manifest metadata) -- it just
has no typed façade, and the caller falls back to `extension_ui` or the raw
channel.

---

## 3. Client extension framework (Stevedore)

Mirrors `asyoulikeit`/`demonstrable-visning`. Beebium has a **single kind** of
client extension (a server-extension adapter), so we need one entry-point
group, not the multi-`kind` machinery -- but we keep the list/describe/create
factory split verbatim.

### 3.1 Entry-point group

```toml
[project.entry-points."beebium.ext"]
aun         = "beebium.ext.aun:Adapter"
piconet     = "beebium.ext.piconet:Adapter"
host-serial = "beebium.ext.host_serial:Adapter"
rpc-serial  = "beebium.ext.rpc_serial:Adapter"
```

The **key is the server manifest `name`** (kebab-case), so
`ListExtensions().name` maps 1:1 to an entry-point lookup. Each plugin's
`__init__` re-exports its concrete class under the uniform alias `Adapter`
(as `asyoulikeit` aliases to `Formatter`).

### 3.2 Base class

`beebium.client.extension.ExtensionAdapter` (ABC). Metadata comes from the
class the way both reference projects do it: **description is the docstring**,
**name is the entry-point key** injected at construction.

```python
class ExtensionAdapter(ABC):
    """Client-side adapter for a Beebium server extension."""

    def __init__(self, name: str, channel: ExtensionChannel, *, extension_id: str = ""):
        self._name = name
        self._channel = channel
        self._extension_id = extension_id

    @property
    def name(self) -> str: ...

    @classmethod
    def describe(cls, *, single_line: bool = False) -> str:
        """Description from the class docstring (cleandoc'd)."""

    @classmethod
    def version(cls) -> str: ...
```

Existing `Aun`/`Piconet`/`HostSerial`/`RpcSerial` become subclasses; their
current bodies are already channel-wrappers, so the change is mechanical.

### 3.3 Factory (instantiation-free listing)

`beebium.client.extension`, following the reference's three verbs. The
list/describe path uses `stevedore.ExtensionManager(invoke_on_load=False)` and
reads classmethods; only `create` instantiates (via `DriverManager`). A single
`on_load_failure_callback` maps Stevedore load errors to `BeebiumError`.

```python
def installed_adapters() -> list[str]: ...              # ExtensionManager.names()
def describe_adapter(name, *, single_line=False) -> str  # class.describe(), no instance
def adapter_type(name) -> type[ExtensionAdapter]          # DriverManager, no instance
def create_adapter(name, channel, extension_id="") -> ExtensionAdapter
```

Name normalisation (`-` <-> `_`) matches the references so kebab keys survive
Stevedore's import-safe lookup.

### 3.4 Runtime bridge

`bbc.extensions` -> an `Extensions` façade holding the `ExtensionChannel` and
the `PeripheralExtensionService` stub:

```python
bbc.extensions.loaded          # list[ExtensionInfo] from ListExtensions (server truth)
bbc.extensions["aun"]          # loaded adapter, resolved via Stevedore, bound to channel+id
bbc.extensions.get("acorn-rtc")# None if not loaded / no adapter installed
```

`__getitem__` looks the name up in `loaded`, resolves an adapter via
`create_adapter`, and binds it to the live channel and the server's
`extension_id` (so multi-instance targeting works, superseding the current
"empty id routes by service name" shortcut).

**Decided:** the hardcoded convenience properties (`bbc.aun`, `bbc.piconet`,
`bbc.host_serial`, `bbc.rpc_serial`) are **removed**. All extension access goes
through the dynamic bridge -- `bbc.extensions["aun"]` -- so the core never
hard-codes an extension name and the plugin model is the only path. (`bbc.serial`,
`bbc.econet`, `bbc.transport` stay: those are *core* services, not extensions.)

`ExtensionInfo` / `ParameterSchemaInfo` / `StorageDevice` become frozen
dataclasses (parsed with the protobuf-map iteration workaround noted in
CLAUDE.md for the `config` map).

### 3.5 Typed access vs generic access (static typing / IDE autocompletion)

A string-keyed registry (`bbc.extensions["aun"]`) can only be typed as the
abstract `ExtensionAdapter` -- fine for code that manipulates extensions
*generically*, but a script that **knows** it drives AUN should not pay an
abstraction tax: it wants `Aun`'s concrete methods, parameter checking and
autocompletion. Resolve the tension by **overloading `__getitem__` on the key
type** -- one access point, two idioms:

```python
from beebium.client import Beebium
from beebium.ext.aun import Aun          # concrete adapter (installed with the AUN client)

with Beebium.connect() as bbc:
    aun = bbc.extensions[Aun]            # statically typed as Aun -> full autocomplete
    aun.add_peer(net=1, station=2, host="10.0.0.5")
    for peer in aun.peers:               # peer: PeerInfo
        ...

    for info in bbc.extensions.loaded:   # info: ExtensionInfo (generic discovery)
        print(info.name, info.config)
    adapter = bbc.extensions["aun"]      # ExtensionAdapter (base) -- generic path
```

The concrete class carries its own server name, so the class *is* the key:

```python
class Aun(ExtensionAdapter):
    EXTENSION_NAME = "aun"               # == server manifest name == entry-point key

A = TypeVar("A", bound="ExtensionAdapter")

class Extensions:
    @property
    def loaded(self) -> list[ExtensionInfo]: ...
    @overload
    def __getitem__(self, key: str) -> ExtensionAdapter: ...   # generic: base type
    @overload
    def __getitem__(self, key: type[A]) -> A: ...             # typed: concrete type
```

| You write | Returns | Resolution path | For |
|---|---|---|---|
| `bbc.extensions[Aun]` | `Aun` | direct -- you handed us the class | code that knows the extension; full static facilities |
| `bbc.extensions["aun"]` | `ExtensionAdapter` | Stevedore, from the installed adapter | generic tooling with only a name string |
| `bbc.extensions.loaded` | `list[ExtensionInfo]` | `ListExtensions` RPC | enumerate/describe loaded extensions |

The class-keyed path **bypasses Stevedore** (you already hold the class): it
reads `EXTENSION_NAME`, confirms the server loaded it (via `loaded`), binds the
channel + `extension_id`, and returns the instance typed as `Aun`. Stevedore
serves only the string path. Both raise `ExtensionNotLoadedError` when the
server didn't load that extension. No `.pyi` stubs and no `cast()` -- adapters
are ordinary classes, so autocomplete works as soon as the concrete adapter is
imported.

**Two spellings, both supported.** The typed retrieval is offered two ways so
callers use whichever reads best; they are exact equivalents (same resolution,
same type, same errors):

```python
aun = bbc.extensions[Aun]     # registry-first: one access point for typed + generic
aun = Aun.attach(bbc)         # concrete-first: reads naturally when AUN is the subject
```

`Aun.attach(bbc)` is a thin `ExtensionAdapter` classmethod
(`@classmethod def attach(cls, bbc) -> Self: return bbc.extensions[cls]`),
inheriting from the base so every adapter gets it for free and it always
returns `Self` (the concrete type). Registry-first keeps a single access point
for code that mixes generic and typed use; concrete-first avoids the
class-as-subscript-key idiom for readers who find it unusual. Generic access
stays string/`loaded`-based as above.

### 3.6 Two categories: peripheral extensions and Econet transports (decided)

Refines §3.1-3.5. The server discovers adapter-bearing extensions through **two
distinct services**, and the client mirrors that split rather than pretending
there is one flat registry:

- **Peripheral extensions** attach to an attachment point (serial-port,
  1mhz-bus, user-port, tube, scsi, ...) and are discovered via
  `PeripheralExtensionService`. Adapters: `rpc-serial`, `host-serial`,
  `acorn-rtc`, `acorn-scsi` (and most future ones).
- **Econet transports** *provide the Econet wire*, are mutually exclusive, and
  are discovered via `EconetTransportService`. Adapters: `aun`, `piconet`.

This split is reflected on every axis, for honesty and to make the wrong pairing
impossible by construction:

| Axis | Peripheral | Econet transport |
|---|---|---|
| Import path | `beebium.ext.peripheral.<name>` | `beebium.ext.econet.<name>` |
| Base class | `PeripheralExtensionAdapter` | `EconetTransportAdapter` |
| Entry-point group | `beebium.ext.peripheral` | `beebium.ext.econet` |
| Discovery | `bbc.extensions.loaded` | `bbc.transport.list()` |
| Typed subscript | `bbc.extensions[AcornRtc]` | `bbc.transport[Aun]` |
| Concrete-first | `AcornRtc.attach(bbc)` | `Aun.attach(bbc)` |

`Adapter.attach(bbc)` is the one uniform spelling that works for either: each
category base implements it to route to the right facade (the adapter knows its
own category). `__getitem__` on `bbc.extensions` is bound to
`PeripheralExtensionAdapter` and on `bbc.transport` to `EconetTransportAdapter`,
so handing a transport adapter to `bbc.extensions[...]` is a *static* type error,
not just a runtime one. Both `ExtensionAdapter` common base and the two category
bases live in `beebium.client.extension`; the stevedore factory takes the
entry-point group as a parameter.

Routing note: peripheral adapters bind the `PeripheralExtension` instance id (it
*is* the ExtensionRpc routing key). Transport adapters route by service name
(`extension_id=""`) -- a transport is a singleton, and its
`EconetTransportService` id targets the *UI* service, not ExtensionRpc.

> Discovered while writing the examples: the earlier flat `beebium.ext.*` /
> `bbc.extensions[...]`-only model (Phase 2) left `aun`/`piconet` unreachable,
> because they never register with `PeripheralExtensionService`. The two-category
> model fixes that structurally.

---

## 4. Organisation: namespace + distributions

Adopt the `demonstrable-visning` shape: a **PEP 420 namespace** `beebium`
(no `beebium/__init__.py` in any distribution) with the app under
`beebium.client` and adapters under the **`beebium.ext.*` namespace** (no
`beebium/ext/__init__.py` either). `beebium.client` is a *regular* package
inside that namespace.

The single `beebium` distribution carries the core **and** the first-party
adapters -- `pip install beebium` is batteries-included. Only extensions **not**
bundled with core (third-party, or our own optional/out-of-tree ones) ship as
their own `beebium-ext-<name>` distributions into the same `beebium.ext`
namespace. There is no separate `beebium-client` distribution and no
metapackage.

```
clients/beebium-python-client/                    # the single beebium distribution root
  pyproject.toml
  src/beebium/                     # PEP 420 namespace (no __init__.py)
    client/                        # regular package: Beebium, Connection, all
      __init__.py                  #   core services, framework, _version.py
      _proto/                      #   core service stubs
      ext/                           # namespace (no __init__.py)
      peripheral/                  # namespace: peripheral extension adapters
        rpc_serial/                #   adapter + its own _proto/ (key "rpc-serial")
        host_serial/  acorn_rtc/  acorn_scsi/
      econet/                      # namespace: Econet transport adapters
        aun/  piconet/
```

> **`packages/` deferred (implemented July 2026).** The design originally nested
> this under `clients/beebium-python-client/packages/beebium/` to make room for sibling
> distributions. In practice the `packages/` monorepo layout only earns its keep
> once a *second* distribution exists, and the "separately deployable" goal is
> already met by the PEP 420 namespace alone -- a third party can ship
> `beebium.ext.foo` into the `beebium.ext` namespace regardless of how our
> repo is laid out. So the single `beebium` distribution stays rooted at
> `clients/beebium-python-client/`; introduce `packages/` when the first out-of-tree adapter
> distribution appears.

- The **`beebium`** distribution owns `beebium.client` (regular package, core
  service stubs) and the bundled `beebium.ext.<name>` adapters (each with its
  own extension proto stubs). It registers all first-party adapters in its own
  `[project.entry-points."beebium.ext"]`.
- A **third-party `beebium-ext-<name>`** distribution ships *only*
  `beebium/ext/<name>/` (no `beebium/__init__.py`, no `beebium/ext/__init__.py`)
  plus its own `beebium.ext` entry point. At import time `beebium` and
  `beebium.ext` aggregate paths across both distributions; `beebium.client` is
  found in the core one. Zero changes to core are needed to add an extension.
- **Package directories use underscores** (`host_serial`), **entry-point keys
  use kebab-case** (`host-serial`) to match the server manifest `name`;
  `normalize_name` bridges the two at lookup time.

**Import consequence (decided -- full namespace):** the public entry point
moves from `from beebium import Beebium` to `from beebium.client import
Beebium`. A namespace package cannot carry code in `beebium/__init__.py`, so
the two cannot coexist. At 0.1.0, pre-PyPI, this break is accepted and
deliberate.

> Considered and rejected: keeping `beebium` a regular core package (to retain
> `from beebium import Beebium`) with only `beebium.ext` as a namespace child.
> That would leave the core itself indivisible, short of the full split.

Each distribution uses the same `src`-layout `pyproject.toml` with a dynamic
version read from a shared `_version.py`, so the monorepo versions in lockstep
(consistent with the repo's bump-my-version scheme).

### 4.1 Proto placement & generation

Split `generate_proto.sh` responsibilities:

- Core protos (video, keyboard, debugger, system, disc, econet,
  econet_transport, serial, indicator, sideways, extension_rpc, extension_ui,
  **peripheral_extension**, **audio**, tube) -> `beebium/client/_proto/`.
- Each extension proto (aun, piconet, rpc_serial, host_serial, and future
  acorn_rtc, scsi_host_adapter) -> `beebium/ext/<name>/_proto/`.

Keep generated stubs **committed** (so wheel builds need no protoc), and add a
CI check that re-generation produces **no diff** -- this closes the drift hole
(§1.2) permanently.

### 4.2 Versioning (lockstep)

**Decided:** everything versions in lockstep with the server monorepo, driven
by the existing `bump-my-version` (`.bumpversion.toml`). The single canonical
version lives in one Python file -- `src/beebium/client/_version.py`
(`__version__ = "..."`) -- read dynamically by the `beebium` distribution's
`pyproject.toml` (`[project] dynamic = ["version"]`). The restructure
**removes** the two current `bump-my-version` targets that no longer exist:
`clients/beebium-python-client/pyproject.toml` (moves to `packages/beebium/pyproject.toml`,
version now dynamic -- not a bump target) and
`clients/beebium-python-client/src/beebium/__init__.py` (gone -- namespace has no
`__init__`). They are replaced by a single target on
`packages/beebium/src/beebium/client/_version.py`. Each future separate
`beebium-ext-<name>` distribution adds its own `_version.py` bump target so it
too stays in lockstep. `.bumpversion.toml` is updated as part of Phase 3.

### 4.3 Test against the wheel, not the source tree

**Principle (load-bearing):** the Python test suite must run against the
code **installed from the built wheel into a fresh virtualenv**, never against
`src/`. Source-tree test runs silently pass over packaging defects --
especially missing package data (the committed `_proto` stubs, `py.typed`),
namespace-package misconfiguration (a stray `__init__.py` collapsing the
namespace so third-party `beebium.ext.*` can't attach), and entry-point
registration errors (an adapter that imports fine from `src/` but isn't
discoverable via `importlib.metadata`). All of those are exactly the failure
modes this restructure introduces, so they must be under test.

Concretely: CI (and a local `scripts/test-wheel.sh`) builds the wheel(s), then
`uv venv` / `python -m venv` a clean environment, `pip install` the wheel plus
its `[dev]` extra, and runs `pytest` with the working directory **outside**
`src/` so imports resolve from site-packages. The namespace seam gets an
explicit test: install `beebium` + a dummy out-of-tree `beebium-ext-*` and
assert both resolve and the extension is discovered. This discipline lands in
Phase 3/4 but the wheel-build harness is introduced early (Phase 0) so every
subsequent phase is validated the right way.

---

## 5. Completeness fixes

- **`PeripheralExtensionService`** -> generate the stub; implement
  `beebium.client.extensions` (§3.4). This is prerequisite to the framework,
  not an optional extra.
- **`AudioService`** -> `beebium.client.audio`: `subscribe()` (streaming) +
  `format()`; wire the stub into `Connection`. Symmetric with `video.py`.
- **`acorn-rtc` / `acorn-scsi`** -> **decided:** add minimal first-party
  adapters in-tree (`beebium/ext/acorn_rtc/`, `beebium/ext/acorn_scsi/`),
  registered in the `beebium` distribution's `beebium.ext` entry points, each
  carrying its own proto stubs. No dangling generated code remains.

---

## 6. Deployment (PyPI)

`.github/workflows/publish-python.yml` builds the sdist and wheel of the
`beebium` distribution and publishes them with **PyPI Trusted Publishing
(OIDC)** via `uv publish --trusted-publishing always` -- no long-lived token is
stored. It is a reusable workflow: `release.yml` calls it on every pushed `v*`
tag after the server packages have built (so the client on PyPI and the servers
on GitHub/Homebrew/Scoop are cut from one tag, as the strict protocol
fingerprint handshake requires), and it can be dispatched by hand against
PyPI or TestPyPI for a chosen ref.

Gates before upload: the "generated == committed" proto check, the wheel
packaging suite run against the installed wheel (`scripts/test-wheel.sh`),
`twine check --strict` on both artifacts, and on tags an assertion that the
wheel's version equals the tag. The publish job runs in the GitHub environment
named after the index (`pypi` / `testpypi`); PyPI's trusted publisher for the
project is registered against this repository, this workflow filename and that
environment name.

The distribution ships the GPL licence text (`COPYING.txt`, a copy of the
repository's) in both sdist and wheel.

---

## 7. Phased plan

Ordered to front-load shippable value and defer the riskiest restructure. Each
phase is independently mergeable.

- **Phase 0 -- Repair proto generation + wheel-test harness (no restructure).**
  Fix `generate_proto.sh` to match the committed tree (add tube, acorn_rtc,
  scsi_host_adapter, peripheral_extension, audio wiring); add a CI
  "regeneration is a no-op" check. Introduce `scripts/test-wheel.sh` and a CI
  job that builds the wheel and runs the suite from a fresh venv (§4.3), so
  every later phase is validated against installed artifacts. Unblocks
  everything; low risk.
- **Phase 1 -- Completeness (flat layout).** Add the `PeripheralExtension`
  discovery client and the `AudioService` client. Immediate value; no API
  reshaping.
- **Phase 2 -- Extension framework (flat layout).** Introduce
  `ExtensionAdapter` + Stevedore factory + `bbc.extensions` bridge; refactor
  aun/piconet/host_serial/rpc_serial into `beebium.ext` entry points, still
  in-tree in the single distribution. Delivers pluggability before packaging
  churn. Adds a `stevedore` dependency.
- **Phase 3 -- Namespace + multi-distribution split.** Restructure into
  `packages/`, create `beebium.client` core + `beebium-ext-*` + `beebium`
  metapackage; move stubs per §4.1; update imports. Highest risk; isolated to
  layout, since the framework already works from Phase 2.
- **Phase 4 -- PyPI trusted publishing.** `publish-python.yml` (OIDC via
  `uv publish`), called from `release.yml` on tags. Done; a matrix over
  distributions is added when a second distribution appears.

---

## 8. Decisions & open questions

Settled (this round):

- **Namespace** -- full split: `beebium` is a PEP 420 namespace; core is
  `beebium.client`; import becomes `from beebium.client import Beebium`.
- **Distributions** -- one `beebium` distribution carries core + all
  first-party adapters (`pip install beebium` is batteries-included). Only
  extensions not bundled with core ship as separate `beebium-ext-<name>`
  distributions. No `beebium-client` distribution, no metapackage.
- **Accessors** -- hardcoded `bbc.aun`/`bbc.piconet`/... removed; extension
  access is only via `bbc.extensions[...]`.
- **Kickoff order** -- Phase 0 (proto repair) then Phase 1 (completeness).
- **`acorn-rtc`/`acorn-scsi`** -- build minimal first-party adapters in-tree
  now (§5).
- **Versioning** -- lockstep via `bump-my-version`; single `_version.py`
  source; `.bumpversion.toml` updated in Phase 3 (§4.2).
- **Testing** -- suite runs against the built wheel in a fresh venv, not the
  source tree; wheel-build harness introduced in Phase 0 (§4.3).

Nothing outstanding -- ready to implement on approval.

---

## 9. Future option: bundle the server binary (not in scope now)

Recorded for later; **not** part of the phases above.

Today the Python client always talks to a `beebium-server` the user installed
and launched separately (`Beebium.connect`) or that it shells out to
(`Beebium.launch`, which needs the server binary already on the machine). Two
convenience packagings could remove that prerequisite:

- **macOS client** -- `Beebium.app` already bundles the server binaries. The
  idea here is **symmetry**: both clients can ship with an embedded server so a
  user needs no separate server install. The macOS side already has this; the
  Python side is the piece below.

- **Platform-specific `beebium-server` wheels** -- ship the compiled
  `beebium-server` (per-OS/arch) inside binary wheels so `pip install
  beebium-server` puts a runnable server on `PATH` (a console entry point).
  This directly serves an existing use-case: a CI job that runs a Python script
  against a Beebium server to exercise a ROM image, with **no** system install
  step -- just `pip install beebium beebium-server` and go.

  Shape: a separate distribution `beebium-server` with platform wheels
  (`cp3x-abi3-manylinux…`, `-macosx…`, `-win_amd64`) built by `cibuildwheel`
  or a bespoke CI matrix, versioned in lockstep like everything else. The pure
  Python `beebium` client stays platform-independent and does **not** depend on
  it; `beebium-server` is an optional convenience install. `Beebium.launch`
  would grow a resolver that prefers a `beebium-server` on `PATH` (which the
  wheel provides) before falling back to an explicit path.

  Caveats to resolve when we take this on: wheel size (the server plus its
  bundled ROMs and `extensions/` tree), the glibc floor already documented for
  the `.deb`/`.tar.gz` server packaging, and code-signing/notarisation for the
  macOS wheel.
