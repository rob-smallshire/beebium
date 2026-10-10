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
/// loss -- never on each keystroke. Because typing does not write back to the
/// draft, the view that owns the field is not re-rendered mid-edit, so the AppKit
/// field editor keeps first responder and the insertion caret stays put.
///
/// It shares one `@FocusState` with its section (keyed by `fieldID`), so another
/// control can release it: clicking a check box, a picker, or the background sets
/// the shared focus to nil, which resigns the field. On resign the field commits
/// the user's edit when they changed it, or refreshes to the current value when
/// they did not -- so it never leaves a stale value on screen (the bug where the
/// hex byte stayed at its old text after a check box changed the byte) and never
/// stays focused with a caret after the user has moved on. Committing a value the
/// user did not type is avoided, so a refresh cannot clobber another control's
/// change.
///
/// This is the Configurator's counterpart to the sidebar's `TextInputField`,
/// which edits locally and dispatches only on commit. Invalid input at commit
/// reverts to the current value and is marked briefly; a valid entry is
/// canonicalised through `format` and handed to `onCommit`.
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
    private var isFocused: Bool { focusedField == fieldID }

    var body: some View {
        TextField(placeholder, text: Binding(get: { model.text }, set: { model.type($0) }))
            .frame(maxWidth: width)
            .foregroundColor(model.invalid ? .red : .primary)
            .overlay(RoundedRectangle(cornerRadius: 4)
                .stroke(model.invalid ? Color.red : Color.clear, lineWidth: 1))
            .focused($focusedField, equals: fieldID)
            .onSubmit { commitEdit() }
            .onChange(of: focusedField) { newFocus in
                if newFocus == fieldID {
                    model.noteFocusGain()
                } else {
                    // Resigned (moved to another field, or released by another
                    // control / the background): commit what the user typed, else
                    // refresh to the current value without committing a stale one.
                    resign()
                }
            }
            .onChange(of: currentDisplay) { newDisplay in
                model.syncExternal(display: newDisplay, focused: isFocused)
            }
    }

    private func commitEdit() {
        if let committed = model.commit(currentDisplay: currentDisplay, parse: parse, format: format) {
            onCommit(committed)
        }
    }

    private func resign() {
        if model.isEdited {
            commitEdit()
        } else {
            model.syncExternal(display: currentDisplay, focused: false)
        }
    }
}
