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

/// Storage configuration section view, editing the draft directly.
///
/// Shows the FDC socket picker (for machines with a socket) and the floppy
/// drives. Edits write straight into `draft.storage`, so the draft is the only
/// source of truth. Drive images are kept as `file://` URIs in the draft (the
/// preset schema's form) and presented to the drive rows as plain paths.
struct StorageSectionView: View {
    @Binding var draft: MachineDraft

    /// Schema describing available options (nil if not yet loaded)
    let schema: StorageSchemaSection?

    /// The model's hard-disc capability, if any (shows the Hard discs group).
    let hardDiscCapability: HardDiscCapability?

    /// Current resolve-preset diagnostics (filtered to the rows that own them).
    var diagnostics: [PresetManager.PresetDiagnostic] = []

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                if let fdcSchema = schema?.fdcSocket {
                    fdcSocketSection(schema: fdcSchema)
                }
                // Floppy drives attach to a controller. With none fitted there
                // is nothing to show, so omit the rows (as the live Storage
                // sidebar does). The drives stay in the draft untouched, so
                // fitting a controller brings them -- and any images -- back.
                if schema?.hasActiveFdc(in: draft) ?? false {
                    floppyDrivesSection
                }
                if let capability = hardDiscCapability {
                    Divider()
                    HardDiscsSectionView(draft: $draft, capability: capability, diagnostics: diagnostics)
                }
            }
            .padding(12)
        }
    }

    // MARK: - FDC Socket Section

    private var fdcSocketId: Binding<String> {
        Binding(
            get: { draft.storage?.fdcSocket?.id ?? "none" },
            set: { newID in
                var storage = draft.storage ?? MachineDraft.Storage()
                storage.fdcSocket = MachineDraft.FdcSocket(
                    id: newID, extra: storage.fdcSocket?.extra ?? [:])
                draft.storage = storage
            }
        )
    }

    @ViewBuilder
    private func fdcSocketSection(schema: FDCSocketSchema) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Disc Controller")
                .font(.headline)

            Picker("", selection: fdcSocketId) {
                ForEach(schema.options) { option in
                    Text(option.label).tag(option.id)
                }
            }
            .pickerStyle(.menu)
            .labelsHidden()

            if let selectedOption = schema.options.first(where: { $0.id == fdcSocketId.wrappedValue }),
               let description = selectedOption.description {
                Text(description)
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        }

        Divider()
    }

    // MARK: - Floppy Drives Section

    private var floppyDrivesSection: some View {
        let drives = draft.storage?.floppyDrives ?? []
        return VStack(alignment: .leading, spacing: 0) {
            ForEach(drives.indices, id: \.self) { index in
                FloppyDriveConfigView(
                    driveNumber: drives[index].drive,
                    imageFilepath: driveImageBinding(index: index))

                if index < drives.count - 1 {
                    Divider()
                        .padding(.horizontal, 12)
                }
            }
        }
    }

    /// Bind a drive's image as a plain filesystem path, bridging to the draft's
    /// `file://` URI form.
    private func driveImageBinding(index: Int) -> Binding<String?> {
        Binding(
            get: {
                guard let uri = draft.storage?.floppyDrives?[index].imageUri else { return nil }
                return Self.path(fromImageUri: uri)
            },
            set: { newPath in
                guard var storage = draft.storage, var drives = storage.floppyDrives,
                      drives.indices.contains(index) else { return }
                drives[index].imageUri = newPath.map { Self.imageUri(fromPath: $0) }
                storage.floppyDrives = drives
                draft.storage = storage
            }
        )
    }

    /// A display/filesystem path from a stored image URI (a `file://` URI becomes
    /// its path; anything else is returned unchanged).
    static func path(fromImageUri uri: String) -> String {
        if uri.hasPrefix("file://"), let url = URL(string: uri) { return url.path }
        return uri
    }

    /// A `file://` URI for a filesystem path, the form a preset stores.
    static func imageUri(fromPath path: String) -> String {
        URL(fileURLWithPath: path).absoluteString
    }
}
