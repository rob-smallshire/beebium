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

/// Commits the field currently being edited before another control acts, so the
/// control sees the committed value and nothing is left focused afterwards.
///
/// This is the one place that enforces the order commit -> release -> control
/// action. Each control (a check box, a picker, Add/Change/Remove, a background
/// click) calls `commitActiveEdit()` at the start of its action instead of trying
/// to release focus itself: with the AppKit-backed field, clearing a @FocusState
/// cannot commit synchronously (the resign and commit are async), so the control
/// would otherwise act on the field's stale value and leave it focused. A
/// `CommittingField` registers its synchronous "commit my edit and resign" when it
/// gains focus and clears it on blur.
@MainActor
final class FieldEditCoordinator: ObservableObject {
    private var flushActive: (() -> Void)?

    /// Called by the focused field: `flush` commits its edit (if any) and resigns,
    /// synchronously, when invoked.
    func fieldFocused(_ flush: @escaping () -> Void) { flushActive = flush }

    /// Called by a field that has lost focus, so a later `commitActiveEdit` is a
    /// no-op rather than flushing a field that is no longer being edited.
    func fieldBlurred() { flushActive = nil }

    /// Commit and resign the field being edited now, synchronously. A no-op when
    /// nothing is focused. Call it at the start of any control's action that reads
    /// or changes shared state, so the control acts on the committed value and
    /// leaves nothing focused.
    func commitActiveEdit() {
        let flush = flushActive
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
    /// field edit to flush.
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
/// It still shares one `@FocusState` with its section (keyed by `fieldID`), so
/// another control can release it by setting the shared focus to nil; this field
/// bridges that SwiftUI focus to the NSTextView's first-responder state. On resign
/// it commits the user's edit when they changed it, or refreshes to the current
/// value when they did not -- so it never leaves a stale value and never commits a
/// value the user did not type over another control's change. Tab moves to the
/// next field, Return commits, Escape reverts.
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
    /// The section's shared focus, so other controls can resign this field.
    @FocusState.Binding var focusedField: String?
    /// This field's identity within the section's shared focus.
    let fieldID: String

    @State private var model: EditableFieldModel<Value>
    /// Holds the backing NSTextView so shared-focus changes can focus or resign it.
    @StateObject private var controller = TemplateFieldController()
    /// The shared coordinator that lets another control commit this field's edit
    /// synchronously before it acts. nil when hosted without one (tests, previews).
    @Environment(\.fieldEditCoordinator) private var fieldEditCoordinator

    init(value: Value, format: @escaping (Value) -> String, parse: @escaping (String) -> Value?,
         placeholder: String = "", width: CGFloat? = 120,
         focusedField: FocusState<String?>.Binding, fieldID: String,
         onCommit: @escaping (Value) -> Void) {
        self.value = value
        self.format = format
        self.parse = parse
        self.placeholder = placeholder
        self.width = width
        self._focusedField = focusedField
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
        .onChange(of: focusedField) { newFocus in
            applySharedFocus(newFocus)
        }
        .onChange(of: currentDisplay) { newDisplay in
            // Refresh to the external value unless the user is mid-edit here --
            // actually editing THIS field (its text view is first responder) AND has
            // typed an uncommitted change. Key the "focused" half on the live AppKit
            // first-responder state, not the shared @FocusState: the latter is
            // written asynchronously, so just after another control committed and
            // released this field (a check box flushing a pending edit) @FocusState
            // can still read this field, which wrongly skipped the refresh and left
            // the field showing the just-typed value while the byte had moved on.
            // The first-responder check is synchronous truth; a released field always
            // refreshes.
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
    /// baseline for "edited" and point the section's shared focus at this field.
    private func focusGained() {
        model.syncExternal(display: currentDisplay, focused: false)
        model.noteFocusGain()
        if focusedField != fieldID { focusedField = fieldID }
        // Let another control commit this field synchronously before it acts.
        fieldEditCoordinator?.fieldFocused { flushAndResign() }
    }

    /// Commit this field's edit (if the user changed it) and resign first
    /// responder, synchronously. Called by the coordinator when another control is
    /// about to act, so the control sees the committed value and nothing stays
    /// focused. The async blur path (`resign`) still runs afterwards but finds the
    /// edit already committed, so it is a no-op refresh.
    private func flushAndResign() {
        if model.isEdited { commitEdit() }
        if let textView = controller.textView,
           textView.window?.firstResponder === textView {
            textView.window?.makeFirstResponder(nil)
        }
        if focusedField == fieldID { focusedField = nil }
    }

    /// Follow a shared-focus change: make the text view first responder when this
    /// field is selected, or resign it (which triggers onBlur -> resign()) when the
    /// focus moved elsewhere or a control cleared it.
    private func applySharedFocus(_ newFocus: String?) {
        guard let textView = controller.textView, let window = textView.window else { return }
        let isFirstResponder = (window.firstResponder === textView)
        if newFocus == fieldID {
            if !isFirstResponder {
                window.makeFirstResponder(textView)
                textView.selectedRange = NSRange(location: 0,
                                                 length: (textView.string as NSString).length)
            }
        } else if isFirstResponder {
            window.makeFirstResponder(nil)
        }
    }

    private func commitEdit() {
        if let committed = model.commit(currentDisplay: currentDisplay, parse: parse, format: format) {
            onCommit(committed)
        }
    }

    /// Escape: revert to the current value and give up focus.
    private func cancelEdit() {
        model.syncExternal(display: currentDisplay, focused: false)
        if focusedField == fieldID { focusedField = nil }
    }

    /// Focus lost (blur, or a shared-focus release): commit the user's edit when
    /// they changed it, else refresh to the current value without committing a
    /// stale one over another control's change.
    private func resign() {
        if model.isEdited {
            commitEdit()
        } else {
            model.syncExternal(display: currentDisplay, focused: false)
        }
        fieldEditCoordinator?.fieldBlurred()
    }
}
