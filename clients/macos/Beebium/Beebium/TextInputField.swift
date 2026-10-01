// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
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

/// Local-state wrapper for `Beebium_TextInput` so typing doesn't fire a
/// dispatch per keystroke. Dispatches the committed string on Return /
/// focus loss; per-keystroke edits stay local.
///
/// Used by `ExtensionViewRenderer.renderTextInput`.
struct TextInputField: View {
    let controlId: String
    let textInput: Beebium_TextInput
    let dispatch: (String, ExtensionDispatchPayload) -> Void

    @State private var localValue: String = ""

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            ExtensionFieldLabel(text: textInput.label, help: textInput.help)
            TextField(textInput.placeholder, text: $localValue)
                .textFieldStyle(.roundedBorder)
                .onSubmit {
                    dispatch(controlId, .string(localValue))
                }
                .onAppear {
                    localValue = textInput.value
                }
                .onChange(of: textInput.value) { newValue in
                    // Server pushed an authoritative new value; if the
                    // user isn't mid-edit (their local matches what the
                    // server last sent), accept the server's update.
                    if localValue == textInput.value {
                        localValue = newValue
                    }
                }
            ExtensionFieldNote(note: textInput.note)
        }
    }
}
