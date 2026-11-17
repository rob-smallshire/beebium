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
import UniformTypeIdentifiers

/// The New Machine configurator's Cassette row, above the floppy drives. The
/// mirror of FloppyDriveConfigView for a tape: browse for or drop a tape, which
/// is written to the draft as `storage.cassette.image_uri`. It carries no
/// controller or drive number (there is one deck). Validation is the shared one
/// -- resolve-preset diagnostics at `/storage/cassette/image_uri`, a missing
/// tape a warning -- so there is no client-side content check here.
struct CassetteConfigView: View {
    /// The row's label, from the schema (never hard-coded).
    let label: String
    /// The tape image types the picker filters by, from the schema.
    let imageTypes: [String]
    /// The tape image, as a plain filesystem path (bridged to the draft's URI).
    @Binding var imageFilepath: String?
    /// resolve-preset diagnostics already filtered to this row.
    var diagnostics: [PresetManager.PresetDiagnostic] = []

    @State private var isDropTargeted = false
    @State private var dropRefusal: DiscDropRefusal?
    @StateObject private var rowError = TransientMessage()

    private var isEmpty: Bool { imageFilepath == nil }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text(label)
                    .font(.headline)
                Spacer()
            }

            if isEmpty {
                emptyContent
            } else {
                loadedContent
            }

            if dropRefusal != nil || rowError.kind != nil {
                DriveStatusLine(refusalMessage: dropRefusal?.message, message: rowError)
            }

            DiagnosticLabels(diagnostics: diagnostics)

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
            occupiedReason: "Clear tape first",
            isTargeted: $isDropTargeted,
            refusal: $dropRefusal,
            accept: { url in accept(url) },
            report: { message in rowError.show(message) }
        ))
        .onChange(of: isDropTargeted) { targeted in
            if targeted { rowError.clear() }
        }
    }

    private var emptyContent: some View {
        Text("Empty - Drop tape image here")
            .font(.caption)
            .foregroundColor(.secondary)
            .frame(maxWidth: .infinity, alignment: .leading)
    }

    private var loadedContent: some View {
        VStack(alignment: .leading, spacing: 4) {
            if let filepath = imageFilepath {
                Text(URL(fileURLWithPath: filepath).lastPathComponent)
                    .font(.subheadline)
                    .fontWeight(.medium)
                    .lineLimit(1)
                    .truncationMode(.middle)
                    .help(filepath)
                    .contextMenu {
                        Button {
                            NSPasteboard.general.clearContents()
                            NSPasteboard.general.setString(filepath, forType: .string)
                        } label: {
                            Label("Copy Path", systemImage: "doc.on.doc")
                        }
                        Button {
                            NSWorkspace.shared.activateFileViewerSelecting(
                                [URL(fileURLWithPath: filepath)])
                        } label: {
                            Label("Reveal in Finder", systemImage: "folder")
                        }
                    }
            }
        }
    }

    private var browseButton: some View {
        Button {
            browseForTape()
        } label: {
            Image(systemName: "folder")
                .font(.system(size: 16))
                .foregroundColor(isEmpty ? .primary.opacity(0.6) : .secondary.opacity(0.3))
        }
        .buttonStyle(.borderless)
        .disabled(!isEmpty)
        .help(isEmpty ? "Browse for a tape image" : "Clear tape first")
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
        .help("Clear tape image")
    }

    private func browseForTape() {
        Task { @MainActor in
            let panel = NSOpenPanel()
            panel.title = "Select Tape Image"
            // The accepted types come from the schema; filtering is a
            // convenience, and the server still judges the format on launch.
            let types = imageTypes.compactMap { UTType(filenameExtension: $0) }
            if !types.isEmpty { panel.allowedContentTypes = types }
            panel.allowsMultipleSelection = false
            panel.canChooseDirectories = false
            if panel.runModal() == .OK, let url = panel.url {
                accept(url)
            }
        }
    }

    /// Store a picked or dropped tape's path, resolving a Finder alias to its
    /// target first so the preset references the real file. resolve-preset is
    /// the validator, so nothing is rejected here.
    private func accept(_ rawURL: URL) {
        dropRefusal = nil
        rowError.clear()
        imageFilepath = rawURL.resolvingDiscImageReference().path
    }
}
