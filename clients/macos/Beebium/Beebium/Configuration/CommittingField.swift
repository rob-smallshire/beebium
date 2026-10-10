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

    init(display: String) { text = display }

    /// Record a keystroke-level change (including a deletion). Typing clears any
    /// invalid marking, so a field stops looking wrong the moment the user starts
    /// correcting it.
    mutating func type(_ newText: String) {
        text = newText
        invalid = false
    }

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
/// This is the Configurator's counterpart to the sidebar's `TextInputField`,
/// which already edits locally and dispatches only on commit. The Configurator's
/// earlier fields instead bound a `TextField` straight to the draft and wrote on
/// every keystroke; each write re-rendered the owning section, and for the fields
/// returned through `AnyView` (extension parameters, the station number) the
/// re-render rebuilt the type-erased subtree and replaced the `NSTextField`,
/// dropping first responder so no caret appeared, while the keyboard byte fields
/// had their text reloaded mid-edit by an `onChange(of: byte)`. One field, used
/// everywhere, removes both failure modes.
///
/// Invalid input at commit reverts to the current value and is marked briefly; a
/// valid entry is canonicalised through `format` and handed to `onCommit`.
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

    @State private var model: EditableFieldModel<Value>
    @FocusState private var focused: Bool

    init(value: Value, format: @escaping (Value) -> String, parse: @escaping (String) -> Value?,
         placeholder: String = "", width: CGFloat? = 120, onCommit: @escaping (Value) -> Void) {
        self.value = value
        self.format = format
        self.parse = parse
        self.placeholder = placeholder
        self.width = width
        self.onCommit = onCommit
        _model = State(initialValue: EditableFieldModel(display: format(value)))
    }

    private var currentDisplay: String { format(value) }

    var body: some View {
        TextField(placeholder, text: Binding(get: { model.text }, set: { model.type($0) }))
            .frame(maxWidth: width)
            .foregroundColor(model.invalid ? .red : .primary)
            .overlay(RoundedRectangle(cornerRadius: 4)
                .stroke(model.invalid ? Color.red : Color.clear, lineWidth: 1))
            .focused($focused)
            .onSubmit(commit)
            .onChange(of: focused) { nowFocused in
                if !nowFocused { commit() }
            }
            .onChange(of: currentDisplay) { newDisplay in
                model.syncExternal(display: newDisplay, focused: focused)
            }
    }

    private func commit() {
        if let committed = model.commit(currentDisplay: currentDisplay, parse: parse, format: format) {
            onCommit(committed)
        }
    }
}
