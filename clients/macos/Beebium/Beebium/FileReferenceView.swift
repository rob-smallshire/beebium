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

import AppKit
import SwiftUI

/// Renders a `Beebium_FileReference` (#144): a document icon and the file's
/// display name, the path as a hover tooltip, a state badge with its text, and a
/// pull-down menu holding the server's own actions plus the renderer's client-side
/// ones -- Reveal in Finder (only when the server shares this host's filesystem)
/// and Copy Path. Labels are rendered verbatim; the renderer never invents a
/// server action the view did not list.
///
/// Used by `ExtensionViewRenderer` for the `.fileReference` arm.
struct FileReferenceView: View {
    let controlId: String
    let fileReference: Beebium_FileReference
    let isServerLocal: Bool
    let dispatch: (String, ExtensionDispatchPayload) -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            // Title line: icon, name, state dot, menu. Most of the time (state
            // OK, empty state_text) this is the whole control.
            HStack(spacing: 6) {
                Image(systemName: "doc")
                    .foregroundColor(.secondary)
                Text(displayName)
                    .lineLimit(1)
                    .truncationMode(.middle)
                if fileReference.state != .unknown {
                    Image(systemName: "circle.fill")
                        .font(.system(size: 8))
                        .foregroundColor(extensionUiIndicatorColor(fileReference.state))
                }
                Spacer(minLength: 4)
                menu
            }
            // The state text (a load error or "not found") sits on its own line
            // beneath the title, not inline after the dot (#144 refinement).
            if !fileReference.stateText.isEmpty {
                Text(fileReference.stateText)
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        // The path lives on the server's host; show it on hover rather than
        // spending a line on it (the #142 panel's full-path line is what this
        // primitive replaces).
        .help(fileReference.path)
    }

    private var displayName: String {
        if !fileReference.displayName.isEmpty { return fileReference.displayName }
        let name = (fileReference.path as NSString).lastPathComponent
        return name.isEmpty ? fileReference.path : name
    }

    // No menu button when there is nothing to offer (e.g. a disabled map file
    // with no server actions): the title line then stands alone.
    @ViewBuilder
    private var menu: some View {
        let items = menuItems
        if !items.isEmpty {
            Menu {
                ForEach(items.indices, id: \.self) { index in
                    // One divider between the server's actions and the renderer's
                    // client-side ones; none at the top or when either group is empty.
                    if index > 0 && isClientItem(items[index]) && !isClientItem(items[index - 1]) {
                        Divider()
                    }
                    menuItem(items[index])
                }
            } label: {
                Image(systemName: "ellipsis.circle")
                    .foregroundColor(.secondary)
            }
            .menuStyle(.borderlessButton)
            .menuIndicator(.hidden)
            .fixedSize()
            .help("File actions")
        }
    }

    private var menuItems: [FileReferenceMenu.Item] {
        FileReferenceMenu.items(
            serverActions: fileReference.actions.map {
                FileReferenceMenu.ServerAction(id: $0.id, title: $0.title)
            },
            isServerLocal: isServerLocal,
            hasPath: !fileReference.path.isEmpty)
    }

    private func isClientItem(_ item: FileReferenceMenu.Item) -> Bool {
        switch item {
        case .reveal, .copyPath: return true
        case .serverAction:      return false
        }
    }

    @ViewBuilder
    private func menuItem(_ item: FileReferenceMenu.Item) -> some View {
        switch item {
        case .serverAction(let id, let title):
            Button(title) { dispatch(controlId, .fileAction(id)) }
        case .reveal:
            Button("Reveal in Finder") {
                NSWorkspace.shared.selectFile(fileReference.path,
                                              inFileViewerRootedAtPath: "")
            }
        case .copyPath:
            Button("Copy Path") {
                NSPasteboard.general.clearContents()
                NSPasteboard.general.setString(fileReference.path, forType: .string)
            }
        }
    }
}
