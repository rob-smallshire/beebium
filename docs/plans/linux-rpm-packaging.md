# Plan: RPM packaging and Fedora (`dnf`) support

> **Status: stage 1 implemented and merged.** The `.rpm` artifact (steps 1-7
> below) is built by the Linux bundle and validated in CI by a Fedora `dnf
> install` smoke leg (all legs green, run 28703277049); it attaches to the next
> tagged Release (the already-cut `v0.1.0` predates it). **Stage 2 (Copr) is the
> outstanding work** — see [Out of scope](#out-of-scope-follow-ups). The steps
> are kept below as the record of what was done.

## Goal

Make the headless Beebium server installable on the Fedora / RHEL family the same
way it already installs on Debian and Arch. Fedora uses **RPM** packages and the
**`dnf`** package manager, so there are two natural stages, mirroring exactly the
sequence that worked for Debian and Windows: **ship the artifact first, add the
channel second.**

1. **RPM on the GitHub Release (this plan).** Emit a self-contained `.rpm`
   (`x86_64` + `aarch64`) from the *same* install tree the `.deb` and `.tar.gz`
   already come from, attach it to the Release, and smoke-test it by installing
   with `dnf` in a clean Fedora container. This gives users
   `sudo dnf install ./beebium-server-*.rpm` with almost no new machinery.
2. **Copr for `dnf install` (later, separate plan).** Stand up a Copr project
   (Fedora's hosted community build/repo service, the analogue of the Homebrew
   tap and Scoop bucket) so users get `dnf copr enable rob-smallshire/beebium &&
   dnf install beebium-server` with per-release rebuilds. Deliberately **out of
   scope here** — noted so the RPM we build now is Copr-ready.

We deliberately do **not** pursue a source-build RPM or official Fedora-repo
inclusion: Fedora's grpc/protobuf packaging churns and Fedora policy dislikes
bundled/static dependencies — exactly the coupling the self-contained static
bundle was designed to avoid. The static bundle sidesteps all of it, and because
it is built on the `debian:trixie` glibc-2.41 floor it runs on Fedora (whose
glibc is newer) unchanged. See [packaging.md](../packaging.md) for the bundle
rationale and the glibc floor.

## Why this is cheap

The `.rpm` is the RPM sibling of the `.deb`: CPack has an `RPM` generator that
lays out the *identical* `cmake --install` tree, so the whole feature is
"configure a second CPack generator + a Fedora smoke leg." The `.deb` path
(`cmake/BeebiumPackaging.cmake`, `cmake/BeebiumCPackOptions.cmake`,
`packaging/debian/{postinst,prerm}`) is the template; the RPM path parallels it
line for line.

Two real differences from the `.deb` to get right:

- **Arch tokens.** RPM names architectures `x86_64` / `aarch64` (Debian uses
  `amd64` / `arm64`). This affects the RPM filename and every CI glob/assertion
  that currently keys on `amd64`/`arm64`.
- **Scriptlet argument semantics.** A `.deb` maintainer script switches on
  `configure` / `remove` (`$1` is an *action* word); an RPM scriptlet gets `$1`
  = the *count* of installed instances (`%post`: 1 = fresh, 2 = upgrade;
  `%preun`: 0 = final removal, 1 = upgrade). So the RPM needs its own
  `%post`/`%preun` scripts rather than reusing the Debian ones — the symlink
  removal must be guarded with `[ "$1" = 0 ]` to stay upgrade-safe.

Everything else (the `/opt/beebium` prefix, the `/usr/bin` symlinks onto the four
servers, auto-derived runtime dependencies, the minimal base-library footprint)
carries over unchanged. RPM's `find-requires` scans the ELF `NEEDED`/symbol
versions and produces versioned `libc.so.6(GLIBC_2.38)` requires automatically —
so the glibc floor is encoded in the package with no extra work, and Fedora's
newer glibc satisfies it.

## Steps

### 1. CPack: add the `RPM` generator (Linux)

`cmake/BeebiumPackaging.cmake` — inside the existing `if(CMAKE_SYSTEM_NAME
STREQUAL "Linux")` block, `list(APPEND CPACK_GENERATOR "RPM")` and set the RPM
metadata to match the `.deb`:

- `CPACK_RPM_FILE_NAME "RPM-DEFAULT"` — canonical
  `beebium-server-<version>-1.<arch>.rpm`.
- `CPACK_RPM_PACKAGE_LICENSE "GPLv3+"`, `CPACK_RPM_PACKAGE_GROUP`,
  `CPACK_RPM_PACKAGE_URL` (the homepage), summary/vendor inherited.
- `CPACK_RPM_PACKAGE_RELOCATABLE OFF` — the package is a fixed `/opt/beebium`
  system install (the scriptlets hardcode that path), like the `.deb`.
- `CPACK_RPM_POST_INSTALL_SCRIPT_FILE` / `CPACK_RPM_PRE_UNINSTALL_SCRIPT_FILE` →
  the new scriptlets below.

### 2. Per-generator layout

`cmake/BeebiumCPackOptions.cmake` — add an `elseif(CPACK_GENERATOR STREQUAL
"RPM")` branch identical to the `DEB` branch: prefix `/opt/beebium`, no top-level
directory. (The `.tar.gz` stays the relocatable single-directory bundle.)

### 3. RPM scriptlets

New `packaging/rpm/postinstall` and `packaging/rpm/preuninstall` (RPM `$1`-count
semantics): `%post` creates the four `/usr/bin` → `/opt/beebium/bin` symlinks
(idempotent `ln -sf`, correct for install *and* upgrade); `%preun` removes them
only on final erase (`[ "$1" = 0 ]`). Same argv[0]-canonicalisation reasoning as
the Debian scripts: a `/usr/bin` symlink resolves back into `/opt/beebium` and
exe-relative discovery of extensions/ROMs/presets keeps working.

### 4. Build image

`docker/linux-bundle/Dockerfile` — add `rpm` to the late packaging-tools apt
layer (the same layer that installs `dpkg-dev`/`fakeroot`), so CPack's RPM
generator can shell out to `rpmbuild`. `rpm` on Debian provides `/usr/bin/rpmbuild`;
building an RPM on a non-RPM distro is supported. Then add `RPM` to the `cpack -G`
generator list and copy `beebium-server*.rpm` into `/out` alongside the `.deb`
and `.tar.gz`.

### 5. CI — build upload + Fedora smoke

`.github/workflows/linux-packages.yml`:

- **Upload:** the per-arch upload filter (`_artifacts/*<arch>*`) keys on the
  Debian token, which the RPM filename (`x86_64`/`aarch64`) does **not** contain.
  Add an `rpm_arch` matrix field and a second upload glob so each leg also
  collects its own `.rpm`. The two RPM arch tokens are mutually exclusive, so the
  cross-arch-contamination filter still holds.
- **Smoke:** a new `smoke-rpm` job mirroring `smoke-deb`, in a clean
  `fedora:latest` container: `dnf install -y ./beebium-server-*.<rpm_arch>.rpm`,
  then `scripts/smoke-installed-tree.sh /opt/beebium` (self-containment + plugin
  discovery + boot), then the Python-client interaction smoke. Fedora ships
  Python ≥3.12, so the client's `requires-python` is satisfied natively; keep the
  `uv`-provided interpreter for parity and determinism with the `.deb` leg.

### 6. Release wiring

`.github/workflows/release.yml` — the publish-boundary "Verify the publishable
artifacts" gate asserts an *exact* file set (currently five). Extend it to seven:
add the two `.rpm` filenames, and an arch assertion (install `rpm` on the runner
and check `rpm -qp --qf '%{ARCH}'` matches the filename), paralleling the existing
`.deb` `Architecture` check. Add the Fedora line to the Release notes body.

### 7. Docs

`docs/packaging.md` — add the `.rpm` to *Package formats*, note Fedora/RHEL now
have a first-class package (not just the `.tar.gz`), add the Fedora smoke leg to
*Validation*/*CI*, and update *Status*. Point the future Copr work at this doc.

## Validation ladder (same shape as the `.deb`)

1. `install_smoke` CTest — unchanged (packaging-format-agnostic).
2. `smoke-installed-tree.sh` — unchanged; the reusable primitive the RPM leg
   calls after `dnf install`.
3. **Build-isolation smoke** — new `smoke-rpm` job installs the *just-built*
   `.rpm` in a clean Fedora container and runs the bundle + interaction smoke.
4. **Publish-boundary gate** — the extended seven-file assertion + RPM arch
   check in `release.yml`, before the draft is created.
5. **Published end-to-end smoke** — (later, with Copr) a Fedora leg in
   `release-smoke.yml` that `dnf install`s from the public channel.

## Out of scope (follow-ups)

- **Copr project** for `dnf copr enable … && dnf install beebium-server` — the
  Fedora analogue of the tap/bucket; the RPM built here is the input.
- A Fedora leg in `release-smoke.yml` once the Release carries `.rpm`s and/or
  Copr is live.
- openSUSE (also RPM, `zypper`) — the same `.rpm` is likely to work; not
  validated here.
</content>
</invoke>
