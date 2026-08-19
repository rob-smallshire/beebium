// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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
import UniformTypeIdentifiers

/// Configuration view for a single floppy drive slot.
///
/// Matches the visual style of DriveRowView in StorageModeView but for
/// preset configuration rather than runtime drive management.
struct FloppyDriveConfigView: View {
    /// Drive number (0, 1, etc.)
    let driveNumber: Int

    /// Binding to the image file path
    @Binding var imageFilepath: String?

    /// Whether drag-drop is currently targeted
    @State private var isDropTargeted = false

    /// Reason a hovering drag will not be accepted, shown while it hovers.
    @State private var dropRefusal: DiscDropRefusal?

    /// Why the last picked or dropped image was not accepted -- brief, and
    /// clears itself so an accidental drop is not stuck on the row.
    @StateObject private var driveError = TransientMessage()

    private var isEmpty: Bool {
        imageFilepath == nil
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            // Drive header - matches StorageModeView
            HStack {
                Text("Floppy \(driveNumber)")
                    .font(.headline)
                Spacer()
            }

            // Content based on state
            if isEmpty {
                emptyContent
            } else {
                loadedContent
            }

            if let message = dropRefusal?.message ?? driveError.brief {
                HStack(spacing: 4) {
                    Image(systemName: dropRefusal != nil
                          ? "nosign" : "exclamationmark.triangle.fill")
                        .font(.caption2)
                    // Brief; the server's fuller reason is a hover away.
                    Text(message)
                        .font(.caption)
                        .lineLimit(1)
                        .truncationMode(.middle)
                }
                .foregroundColor(.red)
                .frame(maxWidth: .infinity, alignment: .leading)
                .help((dropRefusal?.message ?? driveError.detail) ?? message)
                .contentShape(Rectangle())
                .onTapGesture { if dropRefusal == nil { driveError.clear() } }
            }

            // Actions row - matches StorageModeView layout
            HStack(spacing: 8) {
                browseButton
                Spacer()
                clearButton
            }
        }
        .padding(12)
        .background(isDropTargeted ? Color.accentColor.opacity(0.1) : Color.clear)
        .contentShape(Rectangle())
        .onDrop(of: [.fileURL], delegate: DiscImageDropDelegate(
            isSlotEmpty: isEmpty,
            occupiedReason: "Clear disc first",
            isTargeted: $isDropTargeted,
            refusal: $dropRefusal,
            accept: { url in validate(url) },
            report: { message in driveError.show(message) }
        ))
        // A fresh, acceptable drag makes a lingering error beside the point.
        .onChange(of: isDropTargeted) { targeted in
            if targeted { driveError.clear() }
        }
    }

    // MARK: - Content Views

    private var emptyContent: some View {
        Text("Empty - Drop disc image here")
            .font(.caption)
            .foregroundColor(.secondary)
            .frame(maxWidth: .infinity, alignment: .leading)
    }

    private var loadedContent: some View {
        VStack(alignment: .leading, spacing: 4) {
            if let filepath = imageFilepath {
                let filename = URL(fileURLWithPath: filepath).lastPathComponent
                Text(filename)
                    .font(.subheadline)
                    .fontWeight(.medium)
                    .lineLimit(1)
                    .help(filepath)
                    .contextMenu {
                        Button {
                            NSPasteboard.general.clearContents()
                            NSPasteboard.general.setString(filepath, forType: .string)
                        } label: {
                            Label("Copy Path", systemImage: "doc.on.doc")
                        }

                        Button {
                            let url = URL(fileURLWithPath: filepath)
                            NSWorkspace.shared.activateFileViewerSelecting([url])
                        } label: {
                            Label("Reveal in Finder", systemImage: "folder")
                        }
                    }
            }
        }
    }

    // MARK: - Buttons

    private var browseButton: some View {
        Button {
            browseForDisc()
        } label: {
            Image(systemName: "folder")
                .font(.system(size: 16))
                .foregroundColor(isEmpty ? .primary.opacity(0.6) : .secondary.opacity(0.3))
        }
        .buttonStyle(.borderless)
        .disabled(!isEmpty)
        .help(isEmpty ? "Browse for disc image" : "Clear disc first")
    }

    private var clearButton: some View {
        Button {
            imageFilepath = nil
        } label: {
            Image(systemName: "eject.fill")
                .font(.system(size: 14))
        }
        .buttonStyle(.bordered)
        .controlSize(.regular)
        .disabled(isEmpty)
        .help("Clear disc image")
    }

    // MARK: - Actions

    private func browseForDisc() {
        // The picker filter comes from the server (cached after the first
        // ask); running on the main actor keeps NSOpenPanel on its thread.
        Task { @MainActor in
            let panel = NSOpenPanel()
            panel.title = "Select Disc Image"
            panel.allowedContentTypes = await PresetManager.shared.discImageContentTypes()
            panel.allowsMultipleSelection = false
            panel.canChooseDirectories = false

            if panel.runModal() == .OK, let url = panel.url {
                validate(url)
            }
        }
    }

    /// Accept a picked or dropped image only if the server executable
    /// recognises it, the same rule the machine will apply once launched.
    ///
    /// There is no running server to ask before launch, so the server
    /// executable is asked instead, via describe-disc-image. If none can be
    /// found yet the image is accepted unchecked -- the machine still
    /// validates it on launch, and refusing to configure a machine because
    /// presets have not finished loading would be worse.
    private func validate(_ url: URL) {
        driveError.clear()
        guard let executablePath = PresetManager.shared.anyCoreExecutablePath else {
            imageFilepath = url.path
            return
        }
        Task {
            let info = await PresetManager.shared.describeDiscImage(
                path: url.path, executablePath: executablePath)
            if let info, !info.recognised {
                // Brief on the row; the server's exact reason in the tooltip.
                driveError.show("Unrecognised format \(url.lastPathComponent)",
                                detail: info.reason)
            } else {
                // Recognised, or the executable could not be run to say
                // otherwise -- accept and let the launch be the backstop.
                imageFilepath = url.path
            }
        }
    }

}
