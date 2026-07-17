# Versioning and Protocol Compatibility

Beebium is a monorepo with **one version** across the server and all clients.
Two mechanisms keep that coherent:

1. **Source-side** — `bump-my-version` keeps every embedded version string equal
   and tags releases.
2. **Runtime-side** — a **protocol fingerprint** handshake makes a client refuse
   a server whose wire contract differs from the one the client was built
   against.

They are complementary: bumping stamps the versions equal *at release*; the
handshake enforces *contract* compatibility *at connect*.

## Versioning with `bump-my-version`

There is no root `pyproject.toml`, so the configuration is a standalone
`.bumpversion.toml` at the repository root. `current_version` is authoritative;
a bump rewrites every listed file in lockstep, commits, and tags `v<version>`.

### Releasing

```bash
uvx bump-my-version bump minor      # 0.1.0 -> 0.2.0: rewrites files, commits, tags v0.2.0
git push --follow-tags              # push the commit AND the tag
```

`--follow-tags` is required so the tag reaches the remote (configure it once with
`git config push.followTags true`). Validate first with
`uvx bump-my-version bump --dry-run --verbose minor`.

### What it covers

A single version lives in nine places; the config keeps them equal:

| Artifact | File(s) |
|----------|---------|
| Server (C++) | `CMakeLists.txt` `project(VERSION)` → compiled in as `BEEBIUM_VERSION`; `vcpkg.json` |
| Python client | `clients/beebium-python-client/pyproject.toml`; `clients/beebium-python-client/src/beebium/__init__.py` (`__version__`) |
| TypeScript client | `clients/beebium-typescript-client/src/version.ts`; `package.json`; `package-lock.json` |
| macOS GUI | `clients/macos/Beebium/project.yml` and the generated `project.pbxproj` |

The runtime version is a single source per client (`__version__`,
`src/version.ts`); provenance strings derive from it rather than duplicating a
literal.

### The lockfile

`package-lock.json` embeds the version in two of *its own* nodes (root and
`packages[""]`) plus once per dependency. A blind text replace could clobber a
dependency that shares the version, so the config anchors on the package name
(`"name": "@beebium/client", "version": …`) to match only Beebium's own nodes,
and a `json.tool` pre-commit hook re-canonicalises the JSON afterwards. The
anchor must track `package.json`'s `name`: a stale one matches nothing, and the
lockfile then silently stops being bumped.

### Do not add `[skip ci]` to the bump commit

A future release workflow keys on the `v*` tag. `[skip ci]` on the bump commit
would suppress that tag-triggered run, so the bump message is left plain.

## Protocol compatibility: the fingerprint handshake

### Why a fingerprint, not the version number

Semantic versioning only *claims* that `a.b.x` works with `a.b.y`; the version is
a proxy for the contract, and proxies drift. Instead, compatibility is keyed on
the **actual wire contract**: a fingerprint of the protos. This means:

- A **logic-only patch** (no `.proto` change) leaves the fingerprint unchanged,
  so an already-installed client keeps talking to an upgraded server — you can
  ship a server fix without forcing everyone to reinstall the clients.
- **Any** contract change flips the fingerprint and the handshake refuses,
  automatically, with no release discipline to remember.

### What the fingerprint is

It is **semantic**, not textual — invariant to comments, formatting, declaration
order, file names and imports, and sensitive only to the wire contract.
`scripts/protocol_fingerprint.py`:

1. compiles the protos to a `FileDescriptorSet` **without** source info (which
   drops all comments and source locations);
2. normalises it to a canonical model keyed by fully-qualified type name —
   message field `{number, name, type, label}`, enum values, and service method
   signatures `{name, input, output, streaming}`, all sorted;
3. SHA-256s the canonical JSON.

The canonical set is the **always-present** contract: the core service protos
(`src/service/proto/*.proto`) plus `extension_ui.proto`. Per-extension protos
are excluded (their compatibility, if ever needed, is a separate concern).

### One value, injected into all artifacts — never recomputed per language

The Python, TypeScript and C++ stacks generate stubs from **different** proto
subsets (e.g. Python has `sideways` but not `tube`; TypeScript the reverse), so
if each computed its own hash they would never match. Instead
`scripts/sync_protocol_fingerprint.py` computes the fingerprint **once** and
writes the *identical* constant into three committed files:

- `src/service/include/beebium/service/ProtocolFingerprint.hpp` (server)
- `clients/beebium-python-client/src/beebium/_proto/protocol_fingerprint.py`
- `clients/beebium-typescript-client/src/protocol_fingerprint.ts`

These are generated-and-committed like the proto stubs. When a `.proto` changes,
regenerate the stubs and run `python scripts/sync_protocol_fingerprint.py`, then
commit. CI (`.github/workflows/proto-fingerprint.yml`) runs
`sync_protocol_fingerprint.py --check` whenever the protos, the constants, or the
tooling change, and fails if a proto was changed without regenerating the
fingerprint.

### The handshake

The server reports its fingerprint in `SystemInfo.protocol_fingerprint`
(`system.proto`), populated from the compiled-in C++ constant. On connect, both
clients fetch it (`GetSystemInfo`) and compare it to their own compiled-in
constant:

- Python: `Beebium.connect` / `Beebium.launch` → `_verify_protocol()`
- TypeScript: `Beebium.connect` / `Beebium.launch` → `verifyProtocol()`

On any difference they raise `ProtocolMismatchError` with an actionable message,
so an incompatible pairing fails clearly at connect rather than cryptically
mid-call. `PROTOCOL_FINGERPRINT` and `ProtocolMismatchError` are exported from
both client packages.

## Status

**Done:**
- `bump-my-version` configured across all nine version sites, lockfile-aware,
  validated with `--dry-run`.
- Protocol fingerprint computed, committed to three constants, and CI drift-
  checked.
- Server populates the fingerprint; Python and TypeScript clients assert it at
  connect. Validated end to end against a running server (matched connects;
  spoofed mismatch is rejected).

**Remaining / possible future work:**
- A release-tag flow (e.g. `bump-my-version` → tag → CI builds and publishes the
  packages — see [Packaging and Distribution](packaging.md)).
- Per-extension protocol fingerprints, if extension wire contracts ever need to
  be negotiated independently of the core.
