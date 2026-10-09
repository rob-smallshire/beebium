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

/// The "Hard discs" group of the Storage section: the SCSI hard discs attached
/// to the machine, as a projection over the draft's extension instances.
///
/// Each disc is a `scsi-hard-disc` extension in the draft; the adapter is an
/// `acorn-scsi` instance the user never manages directly here (adding the first
/// disc adds it, removing the last disc removes it). Every edit writes straight
/// through to `draft.extensions` via ScsiDiscEditor.
struct HardDiscsSectionView: View {
    @Binding var draft: MachineDraft
    let capability: HardDiscCapability

    private var editor: ScsiDiscEditor { ScsiDiscEditor(capability: capability) }
    private var discs: [ScsiDiscEditor.Disc] { editor.discs(in: draft) }

    /// SCSI IDs used by more than one disc, so rows can flag a clash.
    private var duplicateIds: Set<Int> {
        var counts: [Int: Int] = [:]
        for disc in discs { counts[disc.scsiId, default: 0] += 1 }
        return Set(counts.filter { $0.value > 1 }.keys)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("Hard discs")
                    .font(.headline)
                Spacer()
                Button {
                    draft = editor.draft(addingDiscTo: draft)
                } label: {
                    Label("Add disc", systemImage: "plus")
                }
                .buttonStyle(.borderless)
                .disabled(discs.count >= 8)
                .help("Attach a SCSI hard disc")
            }

            if discs.isEmpty {
                Text("No hard discs. Add one to attach a SCSI drive.")
                    .font(.caption)
                    .foregroundColor(.secondary)
            } else {
                ForEach(discs) { disc in
                    HardDiscRowView(
                        disc: disc,
                        duplicateId: duplicateIds.contains(disc.scsiId),
                        setScsiId: { draft = editor.draft(settingScsiId: $0, forDiscAt: disc.instanceIndex, in: draft) },
                        setImage: { draft = editor.draft(settingImage: $0, forDiscAt: disc.instanceIndex, in: draft) },
                        remove: { draft = editor.draft(removingDiscAt: disc.instanceIndex, from: draft) })
                    if disc.id != discs.last?.id {
                        Divider()
                    }
                }
            }
        }
    }
}

/// One hard-disc row, with its own transient drop state. Accepts a dropped image
/// file through the shared DiscImageDropDelegate (the same handler the floppy
/// rows use); what the file is gets judged downstream by the server.
private struct HardDiscRowView: View {
    let disc: ScsiDiscEditor.Disc
    let duplicateId: Bool
    let setScsiId: (Int) -> Void
    let setImage: (String?) -> Void
    let remove: () -> Void

    @State private var isDropTargeted = false
    @State private var dropRefusal: DiscDropRefusal?
    @StateObject private var dropError = TransientMessage()

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 8) {
                Text("SCSI ID")
                    .font(.subheadline)
                    .foregroundColor(.secondary)
                Picker("", selection: Binding(get: { disc.scsiId }, set: setScsiId)) {
                    ForEach(0..<8, id: \.self) { Text("\($0)").tag($0) }
                }
                .labelsHidden()
                .frame(width: 60)

                if duplicateId {
                    Image(systemName: "exclamationmark.triangle.fill")
                        .foregroundColor(.orange)
                        .help("Another disc uses SCSI ID \(disc.scsiId)")
                }

                Spacer()

                Button(action: remove) {
                    Image(systemName: "trash")
                }
                .buttonStyle(.borderless)
                .help("Remove this hard disc")
            }

            HStack(spacing: 8) {
                imageLabel
                    .frame(maxWidth: .infinity, alignment: .leading)
                Button("Browse…") { browseForImage() }
                    .controlSize(.small)
                if disc.image != nil {
                    Button("Clear") { setImage(nil) }
                        .controlSize(.small)
                }
            }

            if dropRefusal != nil || dropError.kind != nil {
                DriveStatusLine(refusalMessage: dropRefusal?.message, message: dropError)
            }
        }
        .padding(.vertical, 4)
        .background(isDropTargeted ? Color.accentColor.opacity(0.1) : Color.clear)
        .contentShape(Rectangle())
        .onDrop(of: [.fileURL], delegate: DiscImageDropDelegate(
            isSlotEmpty: disc.image == nil,
            occupiedReason: "Clear disc first",
            isTargeted: $isDropTargeted,
            refusal: $dropRefusal,
            accept: { url in setImage(url.path) },
            report: { message in dropError.show(message) }))
        .onChange(of: isDropTargeted) { targeted in
            if targeted { dropError.clear() }
        }
    }

    @ViewBuilder
    private var imageLabel: some View {
        if let name = disc.displayName {
            VStack(alignment: .leading, spacing: 1) {
                Text(name)
                    .font(.subheadline)
                    .lineLimit(1)
                    .truncationMode(.middle)
                    .help(disc.image ?? "")
                if disc.isBundledMaster {
                    Text("Bundled master - a per-machine working copy is used")
                        .font(.caption2)
                        .foregroundColor(.secondary)
                }
            }
        } else {
            Text("Empty - Drop a disc image here")
                .font(.subheadline)
                .foregroundColor(.secondary)
        }
    }

    /// Browse for a hard-disc image file. Stored as an absolute path (the preset
    /// takes an absolute image path as-is); a bare bundled-master name is set by
    /// the catalogue chooser (to come) instead.
    private func browseForImage() {
        let panel = NSOpenPanel()
        panel.title = "Select Hard Disc Image"
        panel.allowedContentTypes = [UTType(filenameExtension: "dat") ?? .data]
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        guard panel.runModal() == .OK, let url = panel.url else { return }
        setImage(url.path)
    }
}
