# Extension UI Architecture

A server-driven, message-passing approach to giving extensions
symmetric reach into every Beebium frontend (macOS / Windows / Linux /
web / Python diagnostics) without per-frontend code per extension.

Status: Implemented on the `extension-ui-framework` branch (2026-04-20).
The framework spine, Piconet pilot, AUN migration, Python client, and
macOS Swift client all landed; manual end-to-end verification on macOS
confirmed the round-trip behaviour for both transports. See
[Implementation Notes](#implementation-notes) at the bottom of this
document for what got built, what was deferred, and where the design
deviated from the proposal.

---

## Problem

Beebium's extension framework lets the C++ server load arbitrary
peripherals and Econet transports at runtime. The CLI surface for
configuring them is already manifest-driven and uniform across
extensions. The runtime surface is not: each frontend needs hand-written
UI for each extension it wants to expose, and there is no path for an
extension built outside the Beebium tree to ship a control panel that
the macOS / Windows / Linux frontends can render.

Concretely, when Piconet ships a `PiconetService` with a `device_path`
field, an `is_connected` boolean, and (proposed) a `SetMode` RPC, every
frontend that wants to surface those needs:

- A hand-written view in its native UI toolkit
- A hand-written gRPC client wiring
- A coordinated release with the extension

That breaks the asymmetry the extension framework exists to remove.
Plug-in extensions should be discoverable and operable the same way
built-in ones are, in every frontend.

## The Shape

Adopt a server-driven UI model with explicit message passing,
modelled on the Elm Architecture (TEA) and on Phoenix LiveView's
production refinement of it:

- **The server is the source of truth.** It holds the extension's
  state, computes a *view* (a tree of controls) from that state, and
  streams the view to subscribed frontends.
- **The client is a thin renderer.** Each frontend translates the
  view tree into native widgets using its own toolkit. It holds no
  business state of its own.
- **All user actions are messages.** A control id plus a typed
  payload, posted back to the server, which mutates state and emits a
  new view.

```
+-------------------+     SubscribeView (stream View)    +-------------+
|                   | ---------------------------------> |             |
|  Extension on     |                                    |  Frontend   |
|  Beebium server   | <--------------------------------- |  (any UI)   |
|                   |     Dispatch(id, payload)          |             |
+-------------------+                                    +-------------+
```

The frontend never decides what an extension's UI looks like. The
extension declares it; the frontend renders it.

## Reference Points

Two existing systems anchor the design.

### Phoenix LiveView (primary analogue)

LiveView has been in production since 2019 and is the closest match
to what we'd build:

- Server holds session state and renders an HTML view tree from it
- The first response is a static fingerprint plus the dynamic slots;
  subsequent updates are *diffs* of the dynamic slots only
- Client mounts the static template once and patches the DOM in place
  using the morphdom library
- User actions arrive as named events (`phx-click`, `phx-change`)
  with optional payload; the server runs an `handle_event/3` callback,
  mutates state, and the view re-renders

What we should borrow:

- Server holds the model. View is a function of state. Always.
- Stream of view updates, not request/response per render.
- Diff-based updates eventually; full-tree updates first (these UIs
  are tiny — a screen or two of controls per extension).
- Stable client-side ids per control, not coordinate-based addressing.
- Versioned views so stale events from a previous render can be
  detected and rejected.

What we should not borrow:

- HTML as the wire format. Ours is proto and the renderer is native.
- Heredocs and macros. We pay a different language tax.
- LiveView's `phx-change` / `phx-blur` / `phx-debounce` flexibility
  in the first cut. Pick one input semantics and ship it.

### Elm (the discipline)

The Elm Architecture's contribution is the typed message:
`Html msg`, where `msg` is the type of every event the view can
produce. The compiler checks that every dispatched message can be
handled. We can't reproduce that in protobuf, but we can emulate it:

- Every control carries a stable string id assigned by the extension
- All events route through one RPC: `Dispatch(extension, id, payload)`
- The server validates the id is known for the current view revision
  and rejects unknown / stale ids cleanly

The discipline is: *one extension, one update function, every event
flows through it*.

## Control Vocabulary

A constrained alphabet, deliberately small. Seven primitives cover the
concrete extensions on the table (Piconet, AUN, future Pi Econet HAT,
peripheral debug surfaces) and probably 90% of anything else.

| Primitive    | Reads as            | Writes as                   |
|--------------|---------------------|-----------------------------|
| `Label`      | text                | -                           |
| `Indicator`  | semantic state + text | -                         |
| `Toggle`     | bool                | bool                        |
| `Button`     | label, enabled flag | (no payload — fire only)    |
| `Choice`     | options + selected  | selected index              |
| `TextInput`  | string + placeholder | string                     |
| `Group`      | optional label, children | -                      |

`Indicator` carries a semantic state — `OK / WARN / ERROR / UNKNOWN` —
not a colour. The frontend chooses how to render that for its
platform's accessibility conventions.

`Group` is the only structural primitive. Nested groups with optional
labels are sufficient for the layouts we need; the precedent is solid
(HTML `<fieldset><legend>`, SwiftUI `Form/Section`, GTK `Frame`). No
explicit grid / stack / spacing primitives in the first cut — render
groups as the toolkit's natural vertical flow with the toolkit's
default spacing.

The vocabulary expands only when an extension needs something the
existing alphabet cannot express. Stretching the alphabet should hurt
slightly each time — that's the forcing function for keeping it small.

## Wire Schema (Sketch)

A new proto, owned by the core service layer, not by any one extension:

```protobuf
service ExtensionUiService {
    // One stream per extension. Server pushes full views (initially)
    // or diffs (later) as state changes.
    rpc SubscribeView(SubscribeViewRequest) returns (stream View);

    // Client-initiated event. Replies indicate whether the event was
    // accepted; the resulting state change shows up on the SubscribeView
    // stream, not in the response.
    rpc Dispatch(DispatchRequest) returns (DispatchResponse);
}

message SubscribeViewRequest {
    string extension_name = 1;  // e.g. "piconet"
}

message View {
    string extension_name = 1;
    uint64 view_revision = 2;   // monotonic per extension; bumped per render
    Control root = 3;            // usually a Group
}

message Control {
    string id = 1;               // stable across renders where semantics persist
    oneof control {
        Label       label       = 2;
        Indicator   indicator   = 3;
        Toggle      toggle      = 4;
        Button      button      = 5;
        Choice      choice      = 6;
        TextInput   text_input  = 7;
        Group       group       = 8;
    }
}

message Label      { string text = 1; }

message Indicator {
    enum State { UNKNOWN = 0; OK = 1; WARN = 2; ERROR = 3; }
    State state = 1;
    string text = 2;
}

message Toggle     { string label = 1; bool value = 2; }
message Button     { string label = 1; bool enabled = 2; }

message Choice {
    string label = 1;
    repeated string options = 2;
    uint32 selected_index = 3;
}

message TextInput {
    string label = 1;
    string value = 2;
    string placeholder = 3;
}

message Group {
    optional string label = 1;
    repeated Control controls = 2;
}

message DispatchRequest {
    string extension_name = 1;
    string control_id = 2;
    uint64 view_revision = 3;   // server rejects if stale
    oneof payload {
        bool   bool_value   = 4;
        string string_value = 5;
        uint32 index_value  = 6;
        google.protobuf.Empty empty = 7;  // buttons
    }
}

message DispatchResponse {
    bool   accepted = 1;
    string error    = 2;
}
```

A discovery RPC on the same service (or on `EconetTransportService` /
`PeripheralExtensionService`) reports which extensions expose a UI, so
a frontend can decide which subscriptions to open.

## Server-Side: Extension API

The `Extension` base gains an optional UI hook:

```cpp
class Extension {
public:
    // Returns the extension's UI provider, or nullptr if it has no UI.
    virtual ExtensionUi* ui() { return nullptr; }
};

class ExtensionUi {
public:
    // Build the current view from the extension's state. Pure function
    // of state -- no side effects, no I/O.
    virtual View build_view() const = 0;

    // Handle an event from a frontend. May mutate extension state.
    // After returning, the framework calls build_view() and pushes the
    // result to all subscribers.
    virtual void handle_event(const std::string& control_id,
                              const Payload& payload) = 0;
};
```

The framework owns the rest: subscription bookkeeping, view-revision
counters, diffing (eventually), and the `Dispatch` validation
gauntlet (extension exists, control id is known for the current view,
payload type matches). Extensions do not touch gRPC directly.

A small `mark_dirty()` helper on `ExtensionUi` lets an extension
notify the framework that state has changed for reasons other than a
dispatched event (e.g. background USB-CDC traffic on Piconet flipping
`is_connected`). The framework debounces dirty notifications and
issues at most one rebuild per tick.

## Client-Side: Renderer

Each frontend implements one `ExtensionViewRenderer` per toolkit:

- Subscribe to `ExtensionUiService.SubscribeView` for each extension
  the frontend wants to surface (typically all of them, or all whose
  manifest declares a UI)
- On each `View` received, walk the tree and produce native widgets.
  Use a stable mapping from `control_id` to widget so updates patch
  in place rather than rebuilding the panel
- Wire each widget's user-action callback to construct a
  `DispatchRequest` carrying the control id, the current view
  revision, and the appropriate payload, then call `Dispatch`
- Display the result: extension panels can live in a sidebar, a
  Settings tab, a separate window, or be inlined in a status pane.
  That choice is the frontend's, not the extension's

The renderer is the only extension-aware code in each frontend, and
it is per-toolkit, not per-extension. Adding a new extension adds zero
client code.

## Pilot: Piconet First

Piconet's UI is small enough to validate the architecture without
fighting layout corner cases:

```
Group(label = "Piconet")
  Label     { text = "Device: /dev/tty.usbmodem101" }
  Indicator { state = OK,  text = "Adapter responsive" }   // serial_open
  Toggle    { label = "Enabled", value = true }            // LISTEN <-> STOP
```

This exercises every leg of the loop:

- **Read-only state** → Label, Indicator (driven by `mark_dirty()`
  when a STATUS event arrives or the serial port closes)
- **State mutation** → Toggle, dispatched as a bool, mapped on the
  server to `SET_MODE LISTEN` or `SET_MODE STOP`
- **Round-trip closure** → after `handle_event`, the extension's
  cached mode changes, `build_view` reflects it, the next `View` push
  carries the updated `Toggle.value`. Clients render the new state
  without round-trip-specific code.

If a frontend can render Piconet's UI and toggling it actually changes
the firmware mode, the architecture is proven.

## Pilot Stretch: AUN

After Piconet, the AUN built-in extension is the natural second test.
Its UI exercises pieces Piconet does not:

- Nested groups (Network / Peers)
- TextInput (peer station, host, port)
- Button + form-style state (Add Peer collects three text inputs)
- Choice (eventually: connection state if we expand beyond bool)

If AUN's UI composes from the seven primitives without forcing new
ones, the vocabulary is correctly sized. If it does not, the gap
identified is the next primitive to add.

## Open Questions

- **Diffs vs. full tree.** Start with full-tree pushes; switch to
  diffs only if measurement shows a problem. These views are tens of
  controls at most, not thousands.
- **TextInput dispatch timing.** Per-keystroke dispatch is chatty;
  on-blur dispatch feels laggy if validation lives server-side. First
  cut: dispatch on blur, accept the latency, revisit if a real
  use-case demands per-keystroke.
- **View ownership during reconfiguration.** When a transport is
  unloaded mid-session (does that ever happen?), what does the client
  see? Probably: the SubscribeView stream ends with `NOT_FOUND`. The
  frontend treats that as "panel disappears."
- **Discovery surface.** A new `ListExtensionUis()` RPC, or a flag
  on the existing `ListExtensions` /  `ListTransports` RPCs? Lean
  toward the flag — fewer round trips, no new service.
- **Validation.** Does the server validate `TextInput` values
  (e.g. station number is a uint8)? Probably yes, with the rejection
  surfaced via a transient `Indicator` rendered next to the input.
  Defer the exact mechanism until the AUN pilot needs it.
- **Action confirmation.** A "Restart adapter" button is destructive.
  Does the protocol express "this control needs confirmation," or is
  that the frontend's call? Lean toward a boolean flag on `Button`
  (`requires_confirmation`); cheaper than a dedicated control.
- **Internationalisation.** All labels are bare strings today. If
  the frontend wants to localise, it needs either translatable keys
  or a way to opt out. Defer; current Beebium UI is English-only.

## Out of Scope (For This Pilot)

- Streaming output (e.g. Piconet's `SubscribeMonitorFrames` packet
  capture). That's not a control panel; it belongs in its own
  RPC and its own frontend window.
- Custom rendering primitives (graphs, scopes, spectrograms).
  Out of vocabulary; if a debug surface needs one, it builds its own
  service.
- Theming, layout direction, sizing hints. The frontend owns
  presentation; the extension owns content.
- Server-pushed dialogs / modals. The protocol stays one-way for
  view, one-way for events. Anything that wants a dialog can render
  one client-side from a `Button` press.

## Verification

Architecture is proven when:

1. Piconet's three-control panel renders identically (modulo native
   styling) in the macOS frontend and the Python diagnostics client,
   driven from the same proto schema.
2. Toggling the panel's `Enabled` switch on either client changes the
   Piconet firmware mode and the change is visible on the other client
   within one stream tick.
3. AUN's panel composes from the same seven primitives with no
   schema changes.
4. Adding a new extension to the tree (or as an out-of-tree plugin)
   adds zero lines of code to any frontend; its panel appears as soon
   as the extension is loaded.

If all four hold, ship it.

## Implementation Notes

Brief walkthrough of where the as-built implementation matches the
design and where it deviates. Written 2026-04-20 after the
`extension-ui-framework` branch landed all stages and slices below.

### What was built (matches the design)

- **Schema** — `src/core/extension-api/proto/extension_ui.proto`. Seven
  control primitives (Label, Indicator, Toggle, Button, Choice,
  TextInput, Group), `View` envelope with monotonic `view_revision`,
  `ExtensionUiService` with `SubscribeView` (server-stream) and
  `Dispatch` (unary). Compiled into a dedicated shared library
  `beebium_extension_ui_proto` so plugins, the server, and tests all
  resolve the same proto descriptors from one .so without registration
  conflicts.
- **Server framework** — `ExtensionUi` abstract base in
  `beebium_extension_api`; `Extension::ui()` virtual returning
  `nullptr` by default; `ExtensionUiServiceImpl` in the service layer
  with a poll-loop `SubscribeView` modelled on
  `IndicatorService::Subscribe` and a Dispatch validation gauntlet
  (extension exists, control id is known for the current view,
  payload variant matches, view revision is current). `mark_dirty()`
  is the bump-revision signal extensions invoke to push a new View.
- **Piconet pilot** — `PiconetUi` in the piconet plugin: device-path
  Label, USB-state Indicator, Enable/Disable Button (was Toggle in
  the original design — see deviations below). Stable per-control
  ids; SwiftUI patches widgets in place across pushes.
- **AUN migration** — `AunUi` in the AUN built-in extension: the
  Connect/Disconnect Button, "Listening on UDP port N" Label, peers
  list. Hardcoded SwiftUI for these in `NetworkModeView` deleted; the
  transport-agnostic header (Connection state, Econet Station + edit
  popover) stays hardcoded in NetworkModeView since those are
  transport-agnostic concerns.
- **Python client** — `clients/beebium-python-client/src/beebium/extension_ui.py`
  with dataclass mirrors of every control type, `subscribe_view`
  iterator, `start_background_subscription` daemon-thread pattern,
  type-dispatched `dispatch(payload=bool|str|int|None)`.
- **macOS Swift client** — `ExtensionUiClient` (Disconnectable,
  callback-stream pattern), `ExtensionViewRenderer` (recursive
  Control → SwiftUI walker), `ExtensionPanelView` (per-extension
  subscription wrapper). Renderer integrated into the Network
  sidebar's existing `NetworkModeView`.

### Where the design deviated

- **Piconet's `Enabled` Toggle became a Button.** The design originally
  used a Toggle ("Enabled" on/off) for the LISTEN/STOP firmware mode.
  This worked functionally but produced an inconsistent UX across the
  two transports — AUN used a Connect/Disconnect Button for the same
  conceptual job. Switched to a Button("Disable"/"Enable") for
  symmetry. Captured the principle in the
  `feedback_state_vs_action_controls.md` memory: prefer Indicator +
  Button over Toggle when state can change for reasons beyond user
  intent.

- **`PiconetBackend::is_connected()` is now mode-aware.** Originally it
  returned just `serial_->is_open()` — the USB physical-layer state.
  Field testing showed the Network sidebar's "Connected" state row
  stayed green even when the user had disabled the transport via the
  Toggle/Button. Changed to `is_serial_open() && mode == LISTEN` so
  both transports' `is_connected()` answer the same user-meaningful
  question ("is the BBC actually in two-way comms with the wire?").
  PiconetUi's Indicator continues to use a separate `is_serial_open()`
  accessor for "is the adapter physically there", distinguishing
  "muted via STOP" (Indicator green, header grey) from "USB unplugged"
  (Indicator red, header grey).

- **Async state changes need per-extension callbacks, not just
  `mark_dirty()`.** The framework's poll loop only sees revision
  changes, and the only way to bump the revision is a synchronous
  `mark_dirty()` call. When state changes async (Piconet's reader
  thread closing the serial port on hot-unplug), the extension needs
  a way to notify its UI from a different thread. Solved per-extension
  by adding an `on_async_state_change` callback to `PiconetBackend`'s
  constructor that `PiconetEconetTransportExtension` wires to
  `ui_.mark_dirty()`. The framework-level question of "should there
  be a generic async-update mechanism" is left open as a future
  refactor — see deferrals below.

- **The transport-agnostic header had to be polled, not pushed.** The
  Connection state row in `NetworkModeView` reads from
  `EconetService.GetEconetStatus.connected`. After the AUN
  Connect/Disconnect button moved into `AunUi` (Slice 2 of Stage 6),
  the Dispatch path no longer goes through `EconetClient`, so the
  header stayed stale on connection toggles. Added a 500 ms
  `refreshStatus()` poll in `EconetClient` as a workaround. The
  proper fix — a `WatchEconetStatus` server-streamed RPC — is
  deferred and tracked separately.

- **AUN map separator `;` requires shell quoting.** The original
  Phase 2 design chose `;` as the inner field separator inside `--aun
  map=net.stn;ip;port` because `:` was already taken for k=v pairs.
  The shell interprets `;` as a command separator unless the argument
  is quoted. The error message in the AUN parser was sharpened to
  mention this gotcha; the deeper fix (refactor `is_list` arg-parser
  to free `,` as the inner separator) is deferred.

### What was deferred

Tracked in project memory; brief summary here.

- **Add Peer / Remove Peer form on the AUN panel.** Designed (TextInput
  × 3 + Button + per-row Remove) but not built. Future direction is
  the Dynamic Station Configuration Protocol plus mDNS/Bonjour
  auto-discovery, not manual peer management.
- **`Toggle.enabled` schema field.** Considered for the no-backend
  state but rejected in favour of suppressing the control entirely.
  Worth revisiting if a future use case needs visible-but-disabled
  controls.
- **Reconnect button + USB device discovery for Piconet.** Hot-unplug
  *detection* works; hot-attach does not. See
  `docs/discussion/piconet-device-discovery.md` for the design.
- **Server-streamed `WatchEconetStatus`** to replace the macOS
  client's 500 ms polling workaround. Modelled on
  `IndicatorService::Subscribe`'s server-side push pattern.
- **Generic async-update mechanism in the framework.** Each extension
  currently wires its own callback from backend to UI. If two or
  three extensions converge on the same pattern, factor it then.
- **Toggle-based vs Button-based action symmetry across all
  extensions.** No new principle to enforce; the existing
  `feedback_state_vs_action_controls.md` guidance is sufficient.
- **View diffing.** Pushes are full-tree today. If push payloads grow
  enough to matter, switch to a LiveView-style diff. Premature today.

## Vocabulary extension (2026-10-01): EditableList and FileReference

The AUN map-file panel (#142) was the first surface that edits a
collection, and building it from Labels, Buttons and ModalEditors
produced a panel that was correct and very busy: every affordance had to
be spelled out in words ("Add peer", "Save to map file", the full file
path). Two primitives fix that, and both are generic: any extension that
owns a list of records (peers, subnet rules, mapped discs, serial ports) or
a file on the server's host wants the same things. They are the eighth
and ninth primitives, and they are the "stretch that should hurt": each
earns its place by replacing a dozen lower-level controls, not by adding
one.

### EditableList

Reads as: a titled list of items, each with primary text, secondary text
(right-aligned caption), an optional subtitle, an optional state, and
per-item flags. Writes as: add, edit, remove, and named per-item actions.

```
message EditableList {
    string title = 1;                 // "Peers", "Subnet rules"
    repeated EditableListItem items = 2;
    bool can_add = 3;                 // shows the "+" affordance
    Control add_editor = 4;           // the editor Control for a new item (fields only;
                                      //   the renderer supplies the commit UI)
    string empty_text = 5;            // shown when items is empty: "No peers"
}

message EditableListItem {
    string id = 1;                    // stable; echoed back in dispatches
    string primary = 2;               // "0.254  192.168.1.10:32768"
    string secondary = 3;             // "map file", "mDNS", "subnet"
    string subtitle = 4;              // the label: "PiEconetBridge FS"
    Indicator.State state = 5;        // UNKNOWN = no indicator; WARN = unreachable, etc.
    bool editable = 6;                // edit affordance; editor below is prefilled
    bool removable = 7;               // "-" affordance
    Control editor = 8;               // prefilled editor for this item, when editable
    repeated EditableListAction actions = 9;  // per-item commands, e.g. "Save to map file"
    string note = 10;                 // short warning shown with the item, optional
}

message EditableListAction {
    string id = 1;
    string title = 2;
    string warning = 3;               // optional confirm text; empty = no confirmation
}
```

Dispatch adds one payload:

```
message EditableListEvent {
    enum Kind { ADD = 0; EDIT = 1; REMOVE = 2; ACTION = 3; }
    Kind kind = 1;
    string item_id = 2;               // empty for ADD
    string action_id = 3;             // for ACTION
    EditorCommit commit = 4;          // for ADD and EDIT: the editor field values
}
```

Renderer contract: a bordered, compact list; rows show primary left,
secondary right in caption style, subtitle beneath in secondary colour,
the state as the platform's small indicator; beneath the list the
platform's standard add/remove control (on macOS the gradient "+ -"
segmented buttons as in System Settings, "-" enabled only with a
removable selection); edit by double-click, Return, or the platform's
edit affordance, opening the item's editor as a sheet or popover with
Cancel and Save (ADD uses the list's `add_editor` and a Save titled
"Add"); per-item actions in the row's context menu, each with its
confirmation when `warning` is set. Labels are rendered verbatim. The
renderer never invents an action the view did not list.

### FileReference

Reads as: a file on the server's host, by path, with a display name and
a state. Writes as: named server-side actions; the renderer adds its own
client-side ones.

```
message FileReference {
    string path = 1;                  // absolute, on the server's host
    string display_name = 2;          // "aun-map.json"; defaults to the path's file name
    Indicator.State state = 3;        // OK = loaded; WARN = missing; ERROR = load error
    string state_text = 4;            // "12 peers, 1 subnet" or the load error
    repeated FileReferenceAction actions = 5;  // server actions: "Reload"
}

message FileReferenceAction {
    string id = 1;
    string title = 2;
}
```

Dispatch payload: `string file_action_id`.

Renderer contract: a small document icon and the display name, the path
as a tooltip, the state indicator beside it, and a pull-down (shortcut)
menu holding the server's actions plus the renderer's own: "Reveal in
Finder" (or the platform equivalent) only when the server shares this
host's filesystem (the existing host-fingerprint gating; see
docs/frontend-local-server-gating.md), and "Copy Path" always.

### Portability rule for both primitives

The wire shape carries intent, never presentation. A primitive names what
the user can do (add, edit, remove, run a named action, reveal a file)
and what state a thing is in (OK, WARN, ERROR); it never names a toolkit
control, an icon, a colour, a keyboard shortcut or a menu style. Each
renderer maps intent to its platform's idiom:

| Intent | macOS (SwiftUI/AppKit) | Linux (Qt, expected) | Windows (WinUI, expected) |
|--------|------------------------|----------------------|---------------------------|
| list with add/remove | bordered list, gradient "+ -" segmented buttons | QListView with QToolButtons below, or QTableView | ListView with a CommandBar |
| edit an item | sheet or popover with Cancel/Save | QDialog | ContentDialog |
| per-item actions | row context menu | context menu | flyout |
| file reference | document icon, pull-down menu, Reveal in Finder | file icon, menu, "Show in file manager" via QDesktopServices | file icon, menu, "Show in Explorer" |
| state | SF Symbols indicator | QStyle standard icon | Segoe icon |

What a renderer may add on its own is limited to client-side actions that
need no server knowledge (reveal, copy path) and the chrome its platform
expects. What it may never add is an action or a field the view did not
list, so the three renderers stay interchangeable against one server.
The same table is the test for any future primitive: if it cannot be
described in all three columns without inventing server semantics, it
is not a primitive yet.

### Effect on the AUN panel

The panel becomes: a FileReference line; an EditableList "Peers" whose
rows carry provenance as secondary text, the label as subtitle,
unreachable as WARN, map-file rows editable and removable, other rows
read-only with a "Save to map file" action (mDNS rows with the
ephemeral-port note); an EditableList "Subnet rules"; one Indicator for a
load or validation error. No free-standing Buttons remain except
Connect/Disconnect.

`extension_ui.proto` is served over ExtensionRpc; whether it is part of
the protocol fingerprint is decided by `scripts/sync_protocol_fingerprint.py`'s
input set, and the implementer confirms either way. New primitives are
additive: an older renderer ignores an unknown control (the `none` case),
so the panel degrades to its remaining Labels rather than breaking.

### Status (2026-10-01): built

Both primitives are implemented (#144). `extension_ui.proto` turned out to
be in the fingerprint input set (the canonical contract is the core service
protos plus `extension_ui`), so adding them moved the protocol fingerprint
and took a minor-version bump. `EditableList` added one Control oneof field
and the `EditableListEvent` dispatch payload; `FileReference` added the other
Control field and the `string file_action_id` payload. The Dispatch gauntlet
validates both (item/action ids and commit fields for a list, the action id
for a file reference) so `handle_event` sees only well-formed events. The AUN
panel is rebuilt on them as described above, and the macOS renderer draws the
two controls natively. Field names and numbers are exactly as specified here.
