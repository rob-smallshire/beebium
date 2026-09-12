# Distributing the macOS app: signed, notarized, drag-to-Applications DMGs

Status: design, September 2026. Decisions settled with the maintainer:
individual Apple Developer Program membership (pending approval); the app's
embedded server payload is unified with the static release bundle; separate
DMGs per architecture (Apple Silicon and Intel). Builds on
`docs/macos-app-packaging.md`, which describes the current self-contained but
ad-hoc-signed bundle.

## 1. Goal

A macOS user downloads `Beebium-<version>-macos-<arch>.dmg` (or runs
`brew install --cask beebium-gui`), drags Beebium.app to Applications, and
double-clicks it. It opens first time -- no Gatekeeper refusals, no
right-click rituals, no System Settings detours -- with the emulator servers,
ROMs, presets and extensions inside the app. The DMG is a release asset,
produced and published by the same automated tail as every other artifact.

## 2. Where we start

- The `.app` is already self-contained (servers + plugins + dependencies under
  `Contents/Resources/servers/`, found via `PresetManager.serversDirpath()`),
  but embeds Homebrew-linked servers whose ~102-dylib dependency graph is
  rewritten by `bundle_dependencies.py`, and everything is ad-hoc signed --
  dev-machine-only by design.
- Since v0.1.6 CI builds **static** macOS server bundles (`macos-bundle.yml`,
  arm64 + x86_64, vcpkg static, explicit deployment floors 11.0/12.0, ad-hoc
  signed, install-smoked). These are the bytes shipped in the tarballs and the
  PyPI wheels.
- CI already builds the GUI app on both macOS runners (ci.yml "Build and Test
  GUI Client"), without server embedding.
- The release tail is fully automated (tag push -> PyPI, GitHub Release,
  Homebrew tap, Scoop bucket) via `publish-release` + `sync-channels`.

## 3. Decisions

| Decision | Choice | Why |
|---|---|---|
| Server payload | The **static release bundle**, verbatim (`bin/ lib/ share/` under `Resources/servers/`) | Byte-identical servers across app, tarball, wheel, Homebrew-bundle; one bundling mechanism; far fewer Mach-Os to sign and notarize; the bundle already passes the install smoke on both arches |
| Architectures | **Two DMGs**: `-macos-arm64` and `-macos-x86_64` | Consistent with every other artifact; half the download of a universal app; arm64 is the first-class target |
| Signing identity | Developer ID Application: Robert Smallshire (individual membership) | Personal project identity |
| Library validation | Sign **all** nested code with the same Team ID, leaf-first; no validation-weakening entitlements | The documented rejection mode is ad-hoc nested code under a Developer ID app; same-team signatures satisfy library validation outright |
| Notarization auth | App Store Connect API key (`.p8` + Key ID + Issuer ID) with `notarytool` | The CI-friendly mechanism; no Apple-ID password in CI |
| Distribution | DMG with Applications-symlink drag-install, signed + notarized + stapled; also a `beebium-gui` **cask** in the existing tap pointing at the DMG | The June naming decision reserved `beebium-gui`; cask and DMG are the same artifact |
| Update mechanism | None in-app (no Sparkle) | The cask updates via `brew upgrade`; direct users re-download; revisit on demand |

## 4. The phases

Phased so that everything except the signing identity lands and is testable
before Apple's approval arrives.

### Phase D1 -- unify the embedded server payload (no certificate needed)

Replace the Homebrew-linked embed path with the static bundle:

- The app's embed phase consumes an **installed static server tree** (the
  same layout as the bundle tarball: `bin/`, `lib/`, `share/beebium/`),
  placed verbatim under `Contents/Resources/servers/`. Locally that tree
  comes from `cmake --install` of a vcpkg-static build; in CI it is the
  `bundle-macos-<arch>` artifact.
- `bundle_dependencies.py`'s graph-rewriting becomes unnecessary for the
  servers (static binaries, `@loader_path`-relative internal libs already
  set by the install rules). What remains of the embed step is a copy plus
  the existing verification (no absolute-path leaks; codesign integrity).
- Swift changes: `PresetManager.serversDirpath()` and any launcher paths
  follow the bundle layout (`servers/bin/beebium-model-b`, ROMs/presets from
  `servers/share/beebium/`), replacing the flat layout.
- The ad-hoc-signed unified app must run end to end on the dev machine:
  launch each machine variant, plugins load (the extensions panel lists
  them), ROMs/presets resolve from the payload.
- Retire or demote `bundle_dependencies.py` (kept only if the dev-tree
  fallback path still wants it; prefer deleting a mechanism over keeping
  two).

### Phase D2 -- CI app build + unsigned DMG (no certificate needed)

- A reusable `macos-app.yml`: matrix over {arm64/macos-14 (floor 11.0),
  x86_64/macos-15-intel (floor 12.0)} -- matching `macos-bundle.yml`'s
  runners and floors; downloads the `bundle-macos-<arch>` artifact; builds
  the app (`xcodegen` + `xcodebuild`, Release configuration) with the embed
  phase pointed at the bundle tree; asserts self-containment (no
  `/opt/homebrew`, no build-tree paths) and `codesign --verify --deep
  --strict` (ad-hoc at this phase).
- DMG assembly: `hdiutil`-based (no third-party action): staging dir with
  `Beebium.app` + `Applications` symlink + background art (reuse The Shape
  of Beebium), `hdiutil create -format UDZO`. Name:
  `Beebium-<version>-macos-<arch>.dmg`.
- Wire into `release.yml`: `macos-app` needs `macos-bundle`; the DMGs join
  the draft release's asset set (and the publish-boundary asset-list gate).
  Until Phase D3 the DMGs are ad-hoc signed and carry a release-notes caveat.
- Smoke: mount the DMG on the runner, copy the app out, launch a bundled
  server binary directly, and run the app headless-ly enough to prove it
  starts (full UI automation is out of scope; `codesign -v` + server boot +
  app process launch/exit is the bar).

### Phase D3 -- Developer ID signing + notarization (needs the membership)

- Secrets: `MACOS_SIGNING_CERT_P12` (base64), `MACOS_SIGNING_CERT_PASSWORD`,
  `ASC_API_KEY_P8` (base64), `ASC_API_KEY_ID`, `ASC_API_ISSUER_ID`,
  `MACOS_TEAM_ID`. Provisioned like the deploy keys; documented in
  `docs/packaging.md`.
- Signing step (replaces ad-hoc in the app build): import the cert into an
  ephemeral keychain; sign leaf-first with Hardened Runtime
  (`--options runtime --timestamp`): every dylib and plugin in the payload,
  every server executable, then the app. Entitlements: start with none
  beyond defaults; add narrowly only if notarization or runtime testing
  demands (record each with its reason).
- Notarize the DMG (`notarytool submit --wait`), `stapler staple` both the
  app inside and the DMG, re-verify with `spctl -a -t open --context
  context:primary-signature` (DMG) and `spctl -a -t exec` (app).
- Gate: the release asset gate asserts the DMGs are stapled (stapler
  validate) before publish.

### Phase D4 -- the `beebium-gui` cask

- Canonical cask in `packaging/homebrew/beebium-gui.rb` (arch-conditional
  url/sha256 for the two DMGs); a sync script sibling to `sync-tap.sh`;
  `sync-channels.yml` gains a step so the cask updates with the formula.
  `brew install --cask beebium-gui` then installs the same DMG bytes.
- Docs: README/downloads section, `docs/macos-app-packaging.md` updated to
  describe the shipped path, `docs/packaging.md` status table.

## 5. Risks and open questions

- **Static servers under the app**: the historical macOS static+dlopen crash
  (duplicate gRPC runtime) was resolved via the single-runtime ExtensionRpc
  design and the bundle smoke proves plugins load standalone; D1 must prove
  the same *through the app*.
- **x86_64 app build**: the Intel runner builds the app in CI today, but the
  unified payload on Intel is only smoke-tested, not user-tested; the Pi-less
  Intel Mac situation means real-hardware validation is Slioch-style remote
  or none. Accepted risk pre-1.0.
- **Notarization latency**: usually minutes, occasionally longer; the release
  tail's wall-clock gains a `--wait`. Acceptable; it runs in parallel with
  nothing else pending.
- **Hardened Runtime unknowns**: JIT-free, no restricted APIs expected in
  either the Swift app or the servers; if notarization flags something, the
  entitlement gets added narrowly with a recorded reason (section 4, D3).
- **Certificate lifetime/rotation**: Developer ID certs last 5 years;
  rotation is a secret swap. The `.p12` never lives outside the secret store.
