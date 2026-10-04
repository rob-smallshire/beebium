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
