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

Gate the UI on the socket having a write-protect switch:
`SocketCapabilities.supports_write_protect` (proto field 5), and, for the runtime
control, only while the slot is currently RAM. Do **not** gate on `supports_ram`
alone — some machines have sideways RAM with no write-protect switch (the B+ 128K
SRAM banks), where a control would appear and always fail. `describe-preset-
schema` also lists `"write_protect"` in each socket's `capabilities` array for
the config UI. (This capability was added after the first spec revision; the
runtime RPC and the launch flag both enforce it server-side too.)

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

- **Model:** in `SidewaysClient.swift`, add `writeProtected: Bool` and
  `supportsWriteProtect: Bool` to `struct Socket` and map them in `mapSocket`
  (from `SocketStatus.writeProtected` / `capabilities.supportsWriteProtect`).
  Show the control only when `supportsWriteProtect` is true and the slot is RAM.
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
  and the socket schema advertises the `"write_protect"` capability
  (`SidewaysSocketSchema` — add a `supportsWriteProtect` computed from the
  capability string, alongside the existing `supportsRom/Ram/Empty`).
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

## UI refinement (round 2 — after first review)

The first implementation worked but the affordance was too heavy (text
Protect/Unprotect button; config checkbox+padlock). Revise both surfaces to a
single **clickable padlock**, column-aligned, plus a menu item — keeping the
server contract and gating unchanged.

Shared rules:

- **One clickable padlock, icon only.** `lock.fill` when write-protected,
  `lock.open` when writable. No text label. The verb goes on `.help(...)`
  ("Write-protect this sideways RAM" / "Allow writes to this sideways RAM").
- **Column alignment.** The padlock sits in a fixed-width **leading** column,
  immediately to the **left** of the ROM/RAM(/Empty) status/kind column, so that
  column stays aligned across rows. Rows that can't be write-protected render an
  empty spacer of the same width (mirroring how the trailing actions menu is
  always rendered for width). Gating is unchanged: shown only when
  `supportsWriteProtect && kind == .ram`.
- **Three-dots menu item too.** Add a write-protect item to the existing
  actions menu (the "…" menu the non-empty rows already show), so the control is
  discoverable there as well as on the padlock. Use the idiomatic macOS
  checkmarked form — a `Toggle("Write-Protect", isOn:)` inside the `Menu` (the
  checkmark shows the current state); label **"Write-Protect"**. Ensure the menu
  is enabled for a write-protectable RAM row even when its other items (Copy
  Path / Reveal) don't apply to blank RAM.

Runtime (Memory sidebar) specifics:

- The padlock is a **Button**, not a SwiftUI `Toggle` bound to state — its glyph
  reflects the **server-reported** `writeProtected` and updates from the RPC
  response, never optimistically on the click. This preserves
  `feedback_state_vs_action_controls` while presenting as a single control. The
  menu item's toggle setter calls the same `setWriteProtect`; its checkmark also
  follows the reported state.

Config (New Machine, Memory tab) specifics:

- **Remove the checkbox+padlock combo**; use the same clickable padlock. Config
  time is pure local state, so the padlock just flips `content.writeProtected`
  (no RPC, cannot fail) — a plain clickable icon is correct here. Place it in the
  same fixed-width leading column, left of the kind dropdown, so the dropdowns
  stay aligned. Add the same "Write-Protect" checkmarked item to that row's
  three-dots actions menu. Keep the existing launch-arg emission and the
  reorder/kind-change clearing behaviour.

Acceptance additions: padlock is icon-only with a tooltip; the status/kind
column is visually aligned across rows (padlock or equal-width spacer); the "…"
menu carries a checkmarked Write-Protect item on write-protectable RAM rows in
both surfaces; runtime padlock still follows server state (non-optimistic).
