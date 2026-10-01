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

/// An information affordance: a small "i" glyph that shows help text in a popover
/// (and as a hover tooltip), never inline (#144 round 2). Shared by TextInput, the
/// editor form fields, and the EditableList title.
struct ExtensionFieldHelpButton: View {
    let help: String
    @State private var isShowing = false

    var body: some View {
        Button {
            isShowing.toggle()
        } label: {
            Image(systemName: "info.circle")
                .foregroundColor(.secondary)
        }
        .buttonStyle(.plain)
        .help(help)
        .popover(isPresented: $isShowing) {
            Text(help)
                .font(.callout)
                .fixedSize(horizontal: false, vertical: true)
                .padding(12)
                .frame(maxWidth: 280)
        }
    }
}

/// A field label with an optional info affordance to its right.
struct ExtensionFieldLabel: View {
    let text: String
    let help: String

    var body: some View {
        if !text.isEmpty || !help.isEmpty {
            HStack(spacing: 4) {
                if !text.isEmpty {
                    Text(text)
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
                if !help.isEmpty {
                    ExtensionFieldHelpButton(help: help)
                }
            }
        }
    }
}

/// A contextual note rendered inline beneath a field in secondary colour, e.g. the
/// ephemeral-port warning on the port field.
struct ExtensionFieldNote: View {
    let note: String

    var body: some View {
        if !note.isEmpty {
            Text(note)
                .font(.caption)
                .foregroundColor(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
    }
}
