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

/// Renaming a machine.
///
/// A sheet rather than an editable window title: SwiftUI's
/// `navigationTitle(Binding<String>)` renders an editable title on iOS but is
/// display-only on macOS, and a hand-built title bar field fought the
/// platform over layout. The name is the window's title, and this is how it
/// is changed.
struct MachineRenameSheet: View {
    /// What the machine is called now.
    let currentName: String

    /// Called with the new name if the user commits a changed, non-empty one.
    let rename: (String) -> Void

    @Environment(\.dismiss) private var dismiss
    @State private var draft: String = ""
    @FocusState private var nameFieldFocused: Bool

    private var trimmed: String {
        draft.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    /// Renaming to nothing is refused by the server, and renaming to the same
    /// name is a wasted round trip.
    private var canRename: Bool {
        !trimmed.isEmpty && trimmed != currentName
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Rename Machine")
                .font(.headline)

            Text("""
                 The name identifies this machine in its window title, in the \
                 Connect dialog, and to anyone browsing for machines on the \
                 network. It belongs to this machine rather than to the preset \
                 it was built from.
                 """)
                .font(.caption)
                .foregroundColor(.secondary)
                .fixedSize(horizontal: false, vertical: true)

            TextField("Machine name", text: $draft)
                .textFieldStyle(.roundedBorder)
                .focused($nameFieldFocused)
                .onSubmit { commit() }

            HStack {
                Spacer()
                Button("Cancel", role: .cancel) { dismiss() }
                    .keyboardShortcut(.cancelAction)
                Button("Rename") { commit() }
                    .keyboardShortcut(.defaultAction)
                    .disabled(!canRename)
            }
        }
        .padding(20)
        .frame(width: 380)
        .onAppear {
            draft = currentName
            nameFieldFocused = true
        }
    }

    private func commit() {
        guard canRename else { return }
        rename(trimmed)
        dismiss()
    }
}

#if DEBUG
struct MachineRenameSheet_Previews: PreviewProvider {
    static var previews: some View {
        MachineRenameSheet(currentName: "Peterhouse", rename: { _ in })
    }
}
#endif
