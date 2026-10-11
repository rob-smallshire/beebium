// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. Beebium is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Beebium.
// If not, see <https://www.gnu.org/licenses/>.

import SwiftUI

/// The Configurator's one focus mechanism: it owns which field is being edited
/// and enforces the order commit -> release -> control action.
///
/// There is no separate `@FocusState`. A `CommittingField` is backed by an
/// NSTextView whose own first-responder state (set by a click or Tab, which
/// AppKit handles) is the single source of truth for focus; the field reports
/// that state here through `fieldFocused`/`fieldBlurred`, so this coordinator
/// knows the focused field's identity and holds its synchronous "commit my edit
/// and resign". Each control (a check box, a picker, Add/Change/Remove, a
/// background click) calls `commitActiveEdit()` at the start of its action
/// instead of trying to release focus itself: with the AppKit-backed field,
/// clearing focus cannot commit synchronously (the resign and commit are async),
/// so the control would otherwise act on the field's stale value and leave it
/// focused.
@MainActor
final class FieldEditCoordinator: ObservableObject {
    /// The id of the field being edited now, or nil. The one place that knows
    /// which field holds focus.
    private(set) var focusedFieldID: String?
    /// The focused field's synchronous "commit my edit (if any) and resign".
    private var flushActive: (() -> Void)?

    /// Called by a field that became first responder: it owns focus now, and
    /// `flush` commits its edit and resigns when invoked.
    func fieldFocused(_ fieldID: String, flush: @escaping () -> Void) {
        focusedFieldID = fieldID
        flushActive = flush
    }

    /// Called by a field that resigned first responder. Clears the active edit
    /// only when the field clearing is the one currently focused, so a late blur
    /// from a field that already lost focus cannot drop a newer field's
    /// registration.
    func fieldBlurred(_ fieldID: String) {
        guard focusedFieldID == fieldID else { return }
        focusedFieldID = nil
        flushActive = nil
    }

    /// Commit and resign the field being edited now, synchronously. A no-op when
    /// nothing is focused. Call it at the start of any control's action that reads
    /// or changes shared state, so the control acts on the committed value and
    /// leaves nothing focused.
    func commitActiveEdit() {
        let flush = flushActive
        focusedFieldID = nil
        flushActive = nil
        flush?()
    }
}

private struct FieldEditCoordinatorKey: EnvironmentKey {
    static let defaultValue: FieldEditCoordinator? = nil
}

extension EnvironmentValues {
    /// The Configurator's field-edit coordinator, injected by ConfigurationEditor.
    /// Optional (nil by default) so a view hosted without it -- a test harness, a
    /// preview -- still builds and runs; there, controls simply have no pending
    /// field edit to flush and each field keeps its own first-responder focus.
    var fieldEditCoordinator: FieldEditCoordinator? {
        get { self[FieldEditCoordinatorKey.self] }
        set { self[FieldEditCoordinatorKey.self] = newValue }
    }
}

/// The pure commit decision for a `CommittingField`, factored out so it is unit
/// testable and so the view and its model share exactly one rule.
enum CommittingFieldLogic {
    /// The outcome of committing a typed `entry` for a field whose current
    /// committed value displays as `currentDisplay`.
    enum Outcome<Value: Equatable>: Equatable {
        /// Parseable: adopt `display` as the text and persist `value`.
        case accept(Value, display: String)
        /// Blank and unparseable: revert to the current display, no marking, no
        /// write (a numeric field refuses a blank without flashing red).
        case revertEmpty
        /// Non-empty and unparseable: revert to the current display, mark invalid.
        case revertInvalid
    }

    static func outcome<Value>(entry: String,
                               parse: (String) -> Value?,
                               format: (Value) -> String) -> Outcome<Value> {
        if let value = parse(entry) {
            return .accept(value, display: format(value))
        }
        return entry.isEmpty ? .revertEmpty : .revertInvalid
    }
}

/// The pure editing model behind `CommittingField`: the text the user has typed
/// and whether the last commit was rejected. It commits only on demand (Return
/// or focus loss), never per keystroke, so the field that owns it does not write
/// to the draft while the user is still typing. That is what keeps the insertion
/// caret alive (see `CommittingField`), and it is unit-testable without SwiftUI.
struct EditableFieldModel<Value: Equatable>: Equatable {
    static func == (lhs: EditableFieldModel, rhs: EditableFieldModel) -> Bool {
        lhs.text == rhs.text && lhs.invalid == rhs.invalid
    }

    /// Exactly what the user has typed; shown verbatim while focused.
    private(set) var text: String
    /// Set when the last commit could not be parsed; cleared by typing or a
    /// successful commit. Drives the brief invalid marking.
    private(set) var invalid = false

    /// The text shown when focus was last gained, so a resign can tell whether
    /// the user actually edited the field (commit their text) or merely tabbed
    /// through / clicked another control (refresh to the current value, never
    /// committing a stale value over another control's change).
    private(set) var focusGainText: String

    init(display: String) {
        text = display
        focusGainText = display
    }

    /// Record a keystroke-level change (including a deletion). Typing clears any
    /// invalid marking, so a field stops looking wrong the moment the user starts
    /// correcting it.
    mutating func type(_ newText: String) {
        text = newText
        invalid = false
    }

    /// Note the text at the moment focus is gained (the baseline for "edited").
    mutating func noteFocusGain() {
        focusGainText = text
    }

    /// Whether the user has changed the text since focus was gained.
    var isEdited: Bool { text != focusGainText }

    /// Adopt an externally-changed display value, but only when not focused -- so
    /// a draft change made elsewhere (a preset reload, a sibling projection of
    /// the same byte, a Reset to default) refreshes the field without fighting an
    /// edit in progress.
    mutating func syncExternal(display: String, focused: Bool) {
        guard !focused else { return }
        text = display
        invalid = false
    }

    /// Commit the typed text. Returns the value to persist on success, or nil
    /// when a revert leaves the draft untouched. `currentDisplay` is the field's
    /// present committed value, which a revert restores.
    mutating func commit(currentDisplay: String,
                         parse: (String) -> Value?,
                         format: (Value) -> String) -> Value? {
        switch CommittingFieldLogic.outcome(entry: text, parse: parse, format: format) {
        case .accept(let value, let display):
            text = display
            invalid = false
            // Settle the field: a commit (Return or focus loss) re-baselines the
            // edited mark, so pressing Return and then clicking another control
            // does not commit the same value a second time.
            focusGainText = display
            return value
        case .revertEmpty:
            text = currentDisplay
            invalid = false
            return nil
        case .revertInvalid:
            text = currentDisplay
            invalid = true
            return nil
        }
    }
}

/// A text field that edits a local string and commits only on Return or focus
/// loss -- never on each keystroke. Typing does not write back to the draft, so
/// the owning view is not re-rendered mid-edit.
///
/// It is backed by the shared `PopoverTextField` (an `NSTextView` with its own
/// insertion point), not a SwiftUI `TextField` (an `NSTextField` that borrows the
/// window's one shared field editor). In the New Machine dialog -- a singleton
/// window hidden and re-shown -- that shared editor goes stale after the first
/// presentation, so an `NSTextField` shows no caret on the second and later opens
/// even though value, focus ring and colours look right (#153,
/// docs/frontend-popover-text-fields.md). An `NSTextView` behaves like the first
/// open every time. There is now one text field implementation in the app:
/// PopoverTextField, used by the sidebar popovers/sheets and by this field.
///
/// Focus has one owner: the NSTextView's own first-responder state, driven by a
/// click or by Tab (AppKit's key-view loop, handled in PopoverTextField). This
/// field reports becoming/resigning first responder to the shared
/// `FieldEditCoordinator` (keyed by `fieldID`), so another control can commit and
/// release it synchronously before it acts (see `commitActiveEdit`) -- a pending
/// edit lands first and nothing is left focused. On resign it commits the user's
/// edit when they changed it, or refreshes to the current value when they did not
/// -- so it never leaves a stale value and never commits a value the user did not
/// type over another control's change. Return commits, Escape reverts and
/// resigns, Tab moves to the next field.
struct CommittingField<Value: Equatable>: View {
    /// The committed value the field reflects (the draft's, or a default in
    /// force). The field shows `format(value)` whenever it is not being edited.
    let value: Value
    let format: (Value) -> String
    let parse: (String) -> Value?
    var placeholder: String = ""
    var width: CGFloat? = 120
    /// Persist a newly-committed value. Not called when a commit reverts.
    let onCommit: (Value) -> Void
    /// This field's identity within the shared coordinator.
    let fieldID: String

    @State private var model: EditableFieldModel<Value>
    /// Holds the backing NSTextView so a commit-before-action can resign it.
    @StateObject private var controller = TemplateFieldController()
    /// The shared coordinator that owns focus identity and lets another control
    /// commit this field's edit synchronously before it acts. nil when hosted
    /// without one (tests, previews), where the field just keeps its own focus.
    @Environment(\.fieldEditCoordinator) private var fieldEditCoordinator

    init(value: Value, format: @escaping (Value) -> String, parse: @escaping (String) -> Value?,
         placeholder: String = "", width: CGFloat? = 120,
         fieldID: String,
         onCommit: @escaping (Value) -> Void) {
        self.value = value
        self.format = format
        self.parse = parse
        self.placeholder = placeholder
        self.width = width
        self.fieldID = fieldID
        self.onCommit = onCommit
        _model = State(initialValue: EditableFieldModel(display: format(value)))
    }

    private var currentDisplay: String { format(value) }

    var body: some View {
        PopoverTextField(
            text: Binding(get: { model.text }, set: { model.type($0) }),
            placeholder: placeholder,
            autofocus: false,
            controller: controller,
            onFocus: { focusGained() },
            onBlur: { resign() },
            reappliesWhenUnfocused: true,
            committedDisplay: currentDisplay,
            isInvalid: model.invalid,
            onSubmit: { commitEdit() },
            onCancel: { cancelEdit() })
        .frame(maxWidth: width)
        .onChange(of: currentDisplay) { newDisplay in
            // Refresh to the external value unless the user is mid-edit here --
            // actually editing THIS field (its text view is first responder) AND has
            // typed an uncommitted change. The "focused" half keys on the live
            // AppKit first-responder state, synchronous truth: just after another
            // control committed and released this field (a check box flushing a
            // pending edit) the text view is no longer first responder, so a
            // released field always refreshes.
            let isFirstResponder = controller.textView.map {
                $0.window?.firstResponder === $0
            } ?? false
            let midEdit = isFirstResponder && model.isEdited
            if !midEdit {
                model.syncExternal(display: newDisplay, focused: false)
            }
        }
    }

    /// The NSTextView became first responder (click or Tab): a field gaining focus
    /// has no in-flight edit yet, so refresh it to the current value first (it may
    /// have gained focus just as the byte changed under it), then record the
    /// baseline for "edited" and register with the coordinator so another control
    /// can commit and release this field synchronously before it acts.
    private func focusGained() {
        model.syncExternal(display: currentDisplay, focused: false)
        model.noteFocusGain()
        fieldEditCoordinator?.fieldFocused(fieldID) { flushAndResign() }
    }

    /// Commit this field's edit (if the user changed it) and resign first
    /// responder, synchronously. Invoked by the coordinator when another control is
    /// about to act, so the control sees the committed value and nothing stays
    /// focused. Resigning triggers the async blur path (`resign`), which finds the
    /// edit already committed, so it is a no-op refresh.
    private func flushAndResign() {
        if model.isEdited { commitEdit() }
        if let textView = controller.textView,
           textView.window?.firstResponder === textView {
            textView.window?.makeFirstResponder(nil)
        }
    }

    private func commitEdit() {
        if let committed = model.commit(currentDisplay: currentDisplay, parse: parse, format: format) {
            onCommit(committed)
        }
    }

    /// Escape: revert to the current value and resign first responder (the
    /// NSTextView does not resign itself on Escape), then tell the coordinator the
    /// field is no longer focused.
    private func cancelEdit() {
        model.syncExternal(display: currentDisplay, focused: false)
        if let textView = controller.textView,
           textView.window?.firstResponder === textView {
            textView.window?.makeFirstResponder(nil)
        }
        fieldEditCoordinator?.fieldBlurred(fieldID)
    }

    /// Focus lost (blur, or a commit-before-action release): commit the user's edit
    /// when they changed it, else refresh to the current value without committing a
    /// stale one over another control's change. Then release focus in the
    /// coordinator.
    private func resign() {
        if model.isEdited {
            commitEdit()
        } else {
            model.syncExternal(display: currentDisplay, focused: false)
        }
        fieldEditCoordinator?.fieldBlurred(fieldID)
    }
}
