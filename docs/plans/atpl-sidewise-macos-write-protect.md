# macOS: sideways write-protect control

## Goal

Add a sideways-RAM **write-protect** control to the macOS front-end. It must be:

- **settable during machine configuration** (New Machine dialog), and
- **toggleable at runtime** on a running machine,
- **general** — driven by slot capability, not hardcoded to the ATPL Sidewise's
  slot 15, so future boards with write-protectable RAM work unchanged.

Today only the ATPL Sidewise (`model-b-atpl-sidewise`) has a write-protectable
slot (slot 15), but nothing in the UI should assume that.

## The contract (server side — already implemented, do not change)

All of this is on the `atpl-sidewise` branch and needs no further server work.

- **Runtime RPC:** `SidewaysService.SetSlotWriteProtect(SetSlotWriteProtectRequest{
  slot: uint32, write_protected: bool }) -> SetSlotWriteProtectResponse{
  success: bool, error: string, write_protected: bool }`. It engages/releases
  the switch under an emulation pause and returns the resulting state. It
  **rejects** a slot that is not currently RAM (`success=false`, `error` set) and
  a machine with no write-protect control.
- **Status field:** `SocketStatus.write_protected` (proto field 9) in
  `GetSlotStatus`. True only for a RAM slot whose switch is engaged.
- **Launch flag:** `--write-protect <slot>` (repeatable, RAM slots only) sets the
  switch's **power-on position**. Rejected at launch for non-RAM-capable slots
  and machines without the control.
- **Protocol fingerprint:** already synced across all four clients (including
  `ProtocolFingerprint.swift`). Do **not** run `sync_protocol_fingerprint.py`;
  it is already correct. Just regenerate the Swift stubs (below).

Gate the UI on a slot being **RAM** (`SocketCapabilities.supports_ram`, or the
mapped `kind == .ram`). There is deliberately no separate "write-protectable"
capability yet — RAM-capable is the signal; the server enforces the rest.

## Deliverable 1 — Swift stubs

Regenerate the Swift stubs for `sideways.proto` so they carry
`setSlotWriteProtect` and `SocketStatus.writeProtected`:

- Files: `clients/macos/Beebium/Beebium/Generated/sideways.pb.swift` and
  `sideways.grpc.swift` (per `clients/macos/Beebium/README.md` §"Regenerating
  gRPC Stubs"; add `sideways.proto` to the protoc invocation).
- **Match the pinned `protoc-gen-grpc-swift` / `swift-protobuf` versions** to the
  SPM-pinned grpc-swift/swift-protobuf, or the whole generated tree churns
  (~6900 lines). Verify the diff is limited to the sideways additions.
- Do not touch `ProtocolFingerprint.swift`.

## Deliverable 2 — runtime control (Memory sidebar)

`MemoryModeView.swift` is read-only today by design; the write-protect control
becomes its single mutable affordance. Keep everything else read-only.

- **Model:** in `SidewaysClient.swift`, add `writeProtected: Bool` (and
  `supportsRam: Bool`) to `struct Socket` and map them in `mapSocket`
  (from `SocketStatus.writeProtected` / `capabilities.supportsRam`).
- **Mutation:** add an async method on `SidewaysClient`, e.g.
  `func setWriteProtect(slot: UInt32, _ protected: Bool) async`, calling
  `client.setSlotWriteProtect(...)`. Follow the existing `fetchSlotStatus`
  `@MainActor` + `do/catch -> errorMessage` pattern (this is the first mutating
  SidewaysService call from Swift). On success, update the socket's
  `writeProtected` from the response (and/or re-fetch); on failure set
  `errorMessage`.
- **View:** in `MemoryModeView` `row(socket:)`, for a RAM socket render an
  **Indicator + Button**, not a Toggle — the house rule (`feedback_state_vs_
  action_controls`; see `docs/howto_write_a_peripheral_extension.md:245`) is that
  a state that can change for reasons beyond the user's click (another client,
  or the launch position) uses an Indicator (shows `writeProtected`) plus a
  Button that performs the action. Precedents: the disc "Read-only" lock
  (`StorageModeView.swift:379` — `Image(systemName: "lock.fill")`), `IndicatorView`,
  and the declarative Indicator/Button in `ExtensionViewRenderer.swift:123,151`.
  Borderless button styling + `.help(...)` as in `StorageModeView`.
- **External changes:** there is no push event for write-protect changes (the
  event stream covers slot-configured / header-changed only). Reflect the state
  from the RPC response and whatever `GetSlotStatus` returns; do not add polling.
  (A `SlotWriteProtectChanged` event is possible future server work — out of scope.)

## Deliverable 3 — configuration control (New Machine dialog)

Config-time is **pure local state** that becomes a launch argument — there is no
running server and no RPC, so here a plain checkbox/Toggle is correct (the
Indicator+Button rule applies only to the runtime, side-effecting control).

- **State:** in `MemoryConfigurationState.swift`, add `writeProtected: Bool` to
  `SocketContent` (meaningful only when `kind == .ram`). Reset/ignore it when the
  kind is not RAM.
- **Launch args:** in `sidewaysLaunchArguments()`
  (`MemoryConfigurationState.swift:291`), additionally emit
  `--write-protect <slot>` for each socket whose configured kind is RAM and whose
  `writeProtected` is true. (Keep emitting the `--sideways <slot>:ram` as today.)
- **View:** in `MemorySectionView.swift`, in `socketRow` / near `kindPicker`
  (line 129), show the write-protect checkbox only when the selected kind is RAM
  and `supportsRam` is true; hide/disable otherwise.
- **Persistence:** the Memory tab does not yet persist to a preset file
  (save-as-preset is a known deferred TODO); write-protect follows suit — it
  applies to the launched session via `--write-protect`. When save-as-preset
  lands, a `write_protected` field is added to the preset `sideways_bank` schema
  then, not now.

## Acceptance

- `xcodebuild build -scheme Beebium` clean; stub diff scoped to sideways.
- Runtime: launch `model-b-atpl-sidewise` with slot 15 RAM; the Memory sidebar
  shows a write-protect indicator + button on slot 15 only; toggling it round-
  trips (indicator follows the server's reported state); no control appears on
  ROM/empty slots or on a Model B.
- Config: in New Machine, choosing RAM for a write-protectable socket reveals the
  checkbox; launching with it ticked boots with `write_protected == true`
  (verify via the sidebar or `GetSlotStatus`).
- A short screen recording of both paths for the review.

## Process

Design/spec: this document (owner: the atpl-sidewise session). Implementation:
`macos-client-developer`. On completion, report back the changed files and the
recording; the atpl-sidewise session reviews the diff before it is considered
done. Work on the `atpl-sidewise` branch. Do not push.
