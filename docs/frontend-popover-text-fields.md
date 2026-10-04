# Editable text fields in popovers and sheets

Platform-independent rule, with macOS specifics, for any front end (#153).

## The problem

A text field shown repeatedly in a popover or sheet anchored to the same window
loses its caret after the first presentation. On macOS 14 the first open shows a
blinking insertion point; the second and later opens show none, even after the
user clicks in the field, because an `NSTextField` edits through the window's one
shared field editor and that shared editor's caret state goes stale when it is
reused across presentations. The field value, focus ring and colours all look
right -- only the caret is missing -- which makes it easy to misdiagnose (we
chased key-window state and an invalidation storm first; neither was the cause).

## The rule

A text field that lives inside a popover or sheet must own its editor, not borrow
the window's shared field editor. On macOS that means backing it with an
`NSTextView` (its own insertion point, created fresh per presentation), not a
bare `NSTextField` or a SwiftUI `TextField` (which is an `NSTextField`
underneath). Use the shared `PopoverTextField` component for these; it is an
`NSTextView` styled as a single-line field, opens prefilled, and commits on
Return / cancels on Escape. The station editor, the machine rename popover and
the extension editor forms (AUN add/edit peer and subnet sheets, ModalEditor text
inputs) all use it. Inline fields that live directly in a window (not in a
popover or sheet) are unaffected.

## Focus in a multi-field form

When a popover or sheet has more than one field, three rules keep focus sane:

- **Exactly one field claims initial focus.** Each `PopoverTextField` would
  otherwise grab focus when the window becomes key, and the last one to run wins,
  so focus jumps to the last field a few hundred milliseconds after opening. Only
  the first editable field sets `autofocus: true` (the single-field default); the
  rest pass `false` and are reached by Tab or a click.
- **A delayed focus assertion must never override the user.** The take-focus step
  runs when the window becomes key, which can be after the user has already
  clicked another field; it must no-op if another field's editor is already first
  responder, or it will yank focus back.
- **Tab moves focus through the key-view loop, it does not type a tab.** A
  single-line `NSTextView` is not a field editor, so the default action for Tab is
  to insert a tab character (replacing a selected value). Handle Tab and
  Shift-Tab explicitly with `selectNextKeyView` / `selectPreviousKeyView`; handle
  Return as commit and Escape as cancel. Open with the value selected (select
  all), as a native field does, so typing replaces it and a click places the caret.

## The regression check

`FocusSelfTest` (macOS, debug-flagged) drives this visually: a caret cannot be
seen from a unit test, so the app photographs its own popovers. Build the app and
run it with the flag set to an output directory:

    BEEBIUM_DEBUG_FOCUS_SELFTEST=/tmp/caret /path/to/Beebium.app/Contents/MacOS/Beebium

It activates itself, opens the station editor (light and dark), the rename
popover and (when a server is given) the picker, synthesises a real click into
each field, captures each popover window several times across the caret's blink
(CGWindowListCreateImage, falling back to `cacheDisplay`), writes `*_frameN.png`,
and quits. Look at the PNGs: a healthy field shows a thin vertical caret bar in
some frames of every open. If later opens never show one, the shared-editor
regression is back.

Set `BEEBIUM_DEBUG_FOCUS_SELFTEST_PORT` to a running server's port as well, and
the rename popover is shown against a real `SystemClient`, so its placeholder
chips and live preview load (they need `ListNamePlaceholders`/`PreviewMachineName`
and show nothing against a disconnected client). Launch a server first, e.g.
`beebium-model-b start --preset <auto-station preset> --port 55123 --wait=api`.

The `BEEBIUM_DEBUG_FOCUS=1` flag separately logs focus/first-responder and
publish-rate diagnostics (`FocusDiagnostics`), which is how the cause was first
narrowed.

### Why a visual harness, and its gotchas

A unit test cannot settle any of this: the `xcodebuild test` host never makes a
window key (an app photographed by the harness does, because it activates
itself), and a caret blinks, so the only reliable check is pixels. The harness
earns its keep -- but it has sharp edges worth knowing before extending it:

- **Capturing your own windows needs no Screen Recording permission.**
  `CGWindowListCreateImage` with the window's number works for the app's own
  windows; keep the `cacheDisplay` fallback for when it returns nil.
- **Synthesise input by POSTING events, never by sending them.** A text field's
  `mouseDown` enters a tracking loop that blocks until it sees the matching
  `mouseUp`, so `window.sendEvent(mouseDown)` hangs the main thread forever.
  Post both (down then up) with `NSApp.postEvent` and let the run loop drain
  them; the same goes for a synthetic Tab key.
- **Reproduce focus transients with the real presentation.** A popover's window
  becomes key immediately; a sheet's becomes key a beat later, like the real
  add-peer / add-subnet editors (EditableList uses a sheet). The "focus jumps to
  the last field" transient only reproduces under the sheet's timing -- capture
  the form the way it is actually presented.
- **Find SwiftUI controls structurally.** A SwiftUI `Button` exposes no usable
  `title`, so locate a placeholder chip by position in the view tree (the first
  button that is not Cancel or Save), not by text.
