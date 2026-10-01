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

/// Renders a `Beebium_ModalEditor` as an inline anchor + pencil button that opens
/// a SwiftUI popover containing the editor tree. The editor tree, its value
/// buffering and the Cancel / Save commit live in the shared `ExtensionEditorForm`
/// (also used by EditableList): Save bundles every buffered value into an
/// `EditorCommit` dispatched to the server, Cancel discards the buffers so the
/// server never sees an aborted edit.
///
/// Used by `ExtensionViewRenderer` for the `.modalEditor` arm.
struct ModalEditorView: View {
    let controlId: String
    let modal: Beebium_ModalEditor
    let dispatch: (String, ExtensionDispatchPayload) -> Void

    @State private var isPresented: Bool = false

    var body: some View {
        HStack(spacing: 6) {
            anchorView(modal.anchor)
                .frame(maxWidth: .infinity, alignment: .leading)
            if modal.editable {
                Button {
                    isPresented = true
                } label: {
                    Image(systemName: "square.and.pencil")
                }
                .buttonStyle(.borderless)
                .help("Edit")
                .popover(isPresented: $isPresented, arrowEdge: .bottom) {
                    ExtensionEditorForm(
                        editor: modal.editor,
                        commitTitle: commitLabel,
                        showCancel: modal.showCancel,
                        onCancel: { isPresented = false },
                        onCommit: { fields in
                            dispatch(controlId, .editorCommit(fields))
                            isPresented = false
                        }
                    )
                    .padding()
                    .frame(minWidth: 280)
                }
            }
        }
    }

    private var commitLabel: String {
        switch modal.commitRole {
        case .save:         return "Save"
        case .add:          return "Add"
        case .UNRECOGNIZED: return "Save"
        }
    }

    // Anchor controls are always-visible and read-only; dispatch is never fired
    // from here. A minimal subset (Label, Indicator) covers every anchor shape we
    // have today.
    private func anchorView(_ control: Beebium_Control) -> AnyView {
        switch control.control {
        case .label(let lbl):
            return AnyView(HStack(spacing: 6) {
                Text(lbl.text)
                    .frame(maxWidth: .infinity, alignment: .leading)
                if !lbl.secondaryText.isEmpty {
                    Text(lbl.secondaryText)
                        .foregroundStyle(.secondary)
                        .font(.caption)
                }
            })
        case .indicator(let ind):
            return AnyView(HStack(spacing: 6) {
                Image(systemName: "circle.fill")
                    .font(.system(size: 8))
                    .foregroundColor(extensionUiIndicatorColor(ind.state))
                Text(ind.text)
                    .foregroundColor(.secondary)
            })
        default:
            return AnyView(EmptyView())
        }
    }
}
