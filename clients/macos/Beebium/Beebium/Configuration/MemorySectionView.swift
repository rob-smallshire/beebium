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

/// Memory (sideways ROM/RAM) configuration section, editing the draft directly.
///
/// Lists the machine's physical sideways sockets, highest boot-priority first,
/// showing the slot number(s) each answers and what it holds. The rows are a
/// projection of the sideways schema over the draft's slot assignments
/// (`SidewaysSocketEditor`); every edit -- kind, image, write-protect, reorder --
/// writes straight back into `draft.sidewaysBank.slots`, so the draft is the
/// only source of truth. The ROM-title cache is the one piece of transient UI
/// state here; it adorns the display and is never written to the draft.
struct MemorySectionView: View {
    @Binding var draft: MachineDraft
    /// The preset as loaded, used to mark sockets changed from it.
    let originalDraft: MachineDraft
    let schema: SidewaysSchemaSection?
    /// Resolves a ROM image to its header (describe-rom), supplied by the host.
    let romResolver: (@MainActor (String) async -> RomHeaderInfo?)?

    @StateObject private var titles = RomTitleCache()

    private typealias Content = SidewaysSocketEditor.Content
    private typealias SocketKind = SidewaysSocketEditor.SocketKind

    private var editor: SidewaysSocketEditor? {
        schema.map { SidewaysSocketEditor(schema: $0) }
    }

    private var currentSlots: [MachineDraft.SidewaysSlot] {
        draft.sidewaysBank?.slots ?? []
    }

    private var rows: [SidewaysSocketEditor.Row] {
        editor?.rows(currentSlots: currentSlots,
                     originalSlots: originalDraft.sidewaysBank?.slots ?? []) ?? []
    }

    /// A key over all loaded images, so title resolution re-runs when they change.
    private var imagesKey: String {
        rows.compactMap { $0.content.image }.sorted().joined(separator: "|")
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            header
                .padding(.horizontal, 12)
                .padding(.top, 12)

            if rows.isEmpty {
                Text("This machine has no configurable sideways sockets.")
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .padding(12)
                Spacer()
            } else {
                // Rows are draggable to reorder: dragging moves a socket's
                // contents (image + kind), not its identity, so the sockets stay
                // in priority order while the ROMs move between them.
                List {
                    ForEach(rows) { row in
                        socketRow(row)
                    }
                    .onMove { source, destination in
                        guard let editor else { return }
                        let newSlots = editor.slots(
                            movingContentsFromOffsets: source, toOffset: destination,
                            currentSlots: currentSlots,
                            originalSlots: originalDraft.sidewaysBank?.slots ?? [])
                        setSlots(newSlots)
                    }
                }
                .listStyle(.plain)
            }
        }
        .onAppear { titles.resolver = romResolver }
        .task(id: imagesKey) {
            for image in rows.compactMap({ $0.content.image }) {
                await titles.resolve(image)
            }
        }
    }

    // MARK: - Header

    private var header: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("Sideways ROM / RAM")
                .font(.headline)
            Text("Listed highest-priority first. At reset the machine enters the language ROM in the highest-numbered slot.")
                .font(.caption)
                .foregroundColor(.secondary)
            if schema?.hasAliasing == true {
                Text("Each socket on this machine answers several aliased slot numbers.")
                    .font(.caption2)
                    .foregroundColor(.secondary)
            }
        }
        .padding(.bottom, 8)
    }

    // MARK: - Socket Row

    private func socketRow(_ row: SidewaysSocketEditor.Row) -> some View {
        VStack(alignment: .leading, spacing: 3) {
            HStack(spacing: 8) {
                Image(systemName: "line.3.horizontal")
                    .font(.system(size: 12))
                    .foregroundColor(.secondary)
                    .frame(width: 14)
                    .help("Drag to reorder this socket's contents")

                Text(row.label)
                    .font(.subheadline)
                    .fontWeight(.medium)
                    .frame(width: 52, alignment: .leading)

                Text(titles.displayText(for: row.content))
                    .font(.subheadline)
                    .foregroundColor(row.content.kind == .empty ? .secondary : .primary)
                    .lineLimit(1)
                    .truncationMode(.middle)
                    .frame(maxWidth: .infinity, alignment: .leading)

                if row.isChanged {
                    Image(systemName: "pencil.circle.fill")
                        .font(.caption)
                        .foregroundColor(.accentColor)
                        .help("Changed from the preset's configuration")
                }

                Group {
                    if row.supportsWriteProtect && row.content.kind == .ram {
                        writeProtectPadlock(row)
                    } else {
                        Color.clear
                    }
                }
                .frame(width: 16)

                kindPicker(row)

                actionsMenu(row)
            }

            Text(captionText(for: row))
                .font(.caption2)
                .foregroundColor(.secondary)
                .padding(.leading, 82)
        }
        .padding(.vertical, 6)
    }

    private func kindPicker(_ row: SidewaysSocketEditor.Row) -> some View {
        let optionCount = (row.supportsRom ? 1 : 0)
            + (row.supportsRam ? 1 : 0)
            + (row.supportsEmpty ? 1 : 0)
        return Picker("", selection: Binding(
            get: { row.content.kind },
            set: { newKind in changeKind(of: row, to: newKind) }
        )) {
            if row.supportsRom { Text("ROM").tag(SocketKind.rom) }
            if row.supportsRam { Text("RAM").tag(SocketKind.ram) }
            if row.supportsEmpty { Text("Empty").tag(SocketKind.empty) }
        }
        .pickerStyle(.menu)
        .labelsHidden()
        .frame(width: 88)
        .disabled(optionCount <= 1)
        .help(optionCount <= 1
              ? "This socket only ever holds \(row.content.kind.label)"
              : "ROM (read-only), RAM (writable), or empty")
    }

    private func writeProtectPadlock(_ row: SidewaysSocketEditor.Row) -> some View {
        Button {
            var content = row.content
            content.writeProtected.toggle()
            setContent(content, for: row)
        } label: {
            Image(systemName: row.content.writeProtected ? "lock.fill" : "lock.open")
                .foregroundColor(row.content.writeProtected ? .accentColor : .secondary)
        }
        .buttonStyle(.borderless)
        .help(row.content.writeProtected
              ? "Allow writes to this sideways RAM"
              : "Write-protect this sideways RAM")
    }

    private func actionsMenu(_ row: SidewaysSocketEditor.Row) -> some View {
        Menu {
            if row.supportsWriteProtect && row.content.kind == .ram {
                Toggle("Write-Protect", isOn: Binding(
                    get: { row.content.writeProtected },
                    set: { var c = row.content; c.writeProtected = $0; setContent(c, for: row) }
                ))
                Divider()
            }
            Button("Browse for ROM image…") { browseForImage(row) }
            Button("Clear") {
                var c = row.content
                c.image = nil
                setContent(c, for: row)
            }
            .disabled(row.content.image == nil)
            Button("Copy Path") {
                if let image = row.content.image {
                    NSPasteboard.general.clearContents()
                    NSPasteboard.general.setString(image, forType: .string)
                }
            }
            .disabled(row.content.image == nil)
            if let filepath = row.content.revealableFilepath {
                Button("Reveal in Finder") {
                    NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: filepath)])
                }
            }
            if row.isChanged {
                Divider()
                Button("Revert to Default") { revert(row) }
            }
        } label: {
            Image(systemName: "ellipsis.circle")
        }
        .menuStyle(.borderlessButton)
        .fixedSize()
        .help("Image actions for this socket")
    }

    private func captionText(for row: SidewaysSocketEditor.Row) -> String {
        let slotsLabel = row.slots.count > 1 ? "Slots" : "Slot"
        let base = "\(slotsLabel) \(row.slotsLabel) · priority \(row.priority)"
        let kinds = titles.kinds(for: row.content)
        if kinds.isEmpty { return base }
        let labelled = kinds.map { $0 == "romfs" ? "ROMFS" : $0 }
        return "\(base) · \(labelled.joined(separator: " · "))"
    }

    // MARK: - Edits (write through to the draft)

    private func setSlots(_ slots: [MachineDraft.SidewaysSlot]) {
        draft.sidewaysBank = MachineDraft.SidewaysBank(
            slots: slots, extra: draft.sidewaysBank?.extra ?? [:])
    }

    private func setContent(_ content: Content, for row: SidewaysSocketEditor.Row) {
        guard let editor else { return }
        setSlots(editor.slots(settingSocket: row.socket, to: content, in: currentSlots))
    }

    /// Change a socket's kind. Selecting ROM on a socket with no image opens a
    /// browse so the draft only ever records a ROM once an image is chosen;
    /// cancelling leaves the socket unchanged.
    private func changeKind(of row: SidewaysSocketEditor.Row, to newKind: SocketKind) {
        var content = row.content
        content.kind = newKind
        switch newKind {
        case .empty:
            content.image = nil
            content.writeProtected = false
            setContent(content, for: row)
        case .ram:
            setContent(content, for: row)
        case .rom:
            if let image = content.image, !image.isEmpty {
                content.writeProtected = false
                setContent(content, for: row)
            } else {
                // No image yet: pick one, else leave the socket as it was.
                browseForImage(row, forcingKind: .rom)
            }
        }
    }

    /// Reset a socket to the preset's original content for it.
    private func revert(_ row: SidewaysSocketEditor.Row) {
        guard let editor else { return }
        let original = editor.content(for: row.socket,
                                      in: originalDraft.sidewaysBank?.slots ?? [])
        setContent(original, for: row)
    }

    /// Browse for a ROM image. For a ROM socket this is the ROM; for a RAM socket
    /// it is an optional startup pre-load image.
    private func browseForImage(_ row: SidewaysSocketEditor.Row, forcingKind: SocketKind? = nil) {
        let panel = NSOpenPanel()
        panel.title = "Select ROM Image"
        panel.allowedContentTypes = [UTType(filenameExtension: "rom") ?? .data]
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false

        guard panel.runModal() == .OK, let url = panel.url else { return }
        var content = row.content
        if let forcingKind { content.kind = forcingKind }
        // Browsing into an empty socket loads it as a ROM (one less step); a RAM
        // socket keeps its kind and treats the image as a preload.
        if content.kind == .empty { content.kind = .rom }
        content.image = url.path
        setContent(content, for: row)
        Task { await titles.resolve(url.path) }
    }
}
