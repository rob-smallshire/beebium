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

import AppKit
import SwiftUI

/// Memory sidebar mode: live view of the running machine's memory state.
///
/// Currently a single "Sideways" section listing each socket priority 15 -> 0
/// with the ROM's title (extracted from the header), a ROM/RAM/- status, and
/// a read-only actions menu. Future sections (Shadow, ANDY, ...) live as
/// additional subviews beneath; cartridge slots will get their own when a
/// machine variant with cartridge slots lands.
///
/// Read-only by design: changing what's in a slot at runtime is intentionally
/// out of scope (see docs/preset-schema.md#sideways-romram-bank).
struct MemoryModeView: View {
    @ObservedObject var sidewaysClient: SidewaysClient

    /// Whether the server is on this host. A slot's image name is an
    /// absolute path on the server, so opening it here only means anything
    /// when "here" and "there" are the same filesystem.
    let isServerLocal: Bool

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                // The fitted OS (MOS) ROM, when the server reports one. Above
                // Sideways, since the OS is the machine's core firmware and on
                // the Master its banks 9-15 are the MOS itself.
                if let osRom = sidewaysClient.osRom {
                    osSection(osRom)
                }
                if !sidewaysClient.isLoaded {
                    loadingView
                } else if sidewaysClient.sockets.isEmpty {
                    noSidewaysView
                } else {
                    sidewaysSection
                }
                // Future sections (Shadow / ANDY / cartridges) sit here.
            }
            .padding(.vertical, 4)
        }
    }

    // MARK: - OS section

    /// The fitted OS ROM: one read-only row laid out exactly like a sideways
    /// socket row (shared `memoryRow`, so the two cannot drift) -- the MOS title
    /// in the title column, a blank slot column, the "ROM" signifier, and the
    /// sockets' Copy Path / reveal actions under the same local-filesystem
    /// gating. No slot number and no bare ROM name: the sockets manage without
    /// one and so does the OS. The server-side path is a tooltip on the row (and
    /// never on its action button) only when the server shares this filesystem,
    /// like every other path here.
    @ViewBuilder
    private func osSection(_ osRom: SidewaysClient.OsRom) -> some View {
        sectionHeader("OS")
        memoryRow(osRowContent(osRom))
        Divider().padding(.leading, 16)
    }

    /// The OS row's display fields. The slot column is blank (the OS has no slot
    /// number) and the status is always "ROM" (kept for symmetry with the
    /// sockets). The title is the manifest title, falling back to the ROM/file
    /// name only when the server reported no title -- never shown as a second line.
    func osRowContent(_ osRom: SidewaysClient.OsRom) -> RowContent {
        RowContent(
            slot: "",
            title: osRom.title.isEmpty ? osRom.subtitle : osRom.title,
            status: "ROM",
            imageName: osRom.imageUri,
            writeGroup: nil,
            hideGroup: nil,
            tooltip: osRomTooltip(osRom))
    }

    private func osRomTooltip(_ osRom: SidewaysClient.OsRom) -> String {
        var lines: [String] = []
        if !osRom.title.isEmpty { lines.append(osRom.title) }
        lines.append(osRom.subtitle)
        // The image_uri is a path on the server, so it only means anything here
        // when the server shares this filesystem -- the same rule the slot
        // Reveal and Copy Path honour.
        if isServerLocal && !osRom.imageUri.isEmpty {
            lines.append(osRom.imageUri)
        }
        return lines.joined(separator: "\n")
    }

    // MARK: - Sideways section

    @ViewBuilder
    private var sidewaysSection: some View {
        sectionHeader("Sideways")
        ForEach(sidewaysClient.sockets) { socket in
            row(socket: socket)
            if socket.id != sidewaysClient.sockets.last?.id {
                Divider().padding(.leading, 16)
            }
        }
    }

    private func sectionHeader(_ title: String) -> some View {
        Text(title)
            .font(.caption)
            .fontWeight(.semibold)
            .foregroundColor(.secondary)
            .textCase(.uppercase)
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(.horizontal, 16)
            .padding(.top, 8)
            .padding(.bottom, 4)
    }

    private func row(socket: SidewaysClient.Socket) -> some View {
        memoryRow(rowContent(for: socket))
    }

    /// A socket's display fields: its priority as the slot column, the ROM title
    /// (or filename, or blank), the ROM/RAM/- status, the source path for Copy
    /// Path / reveal, and the covering protection groups that make the menu live.
    func rowContent(for socket: SidewaysClient.Socket) -> RowContent {
        // Write-protect is meaningful only for a RAM slot covered by a group -- a
        // ROM (or empty) slot is inherently write-protected. Toggling moves every
        // RAM slot in the group together. Hide (read-protect) is meaningful for a
        // ROM too (S1's use case is hiding a hung ROM), so it is not gated on type.
        let writeGroup = socket.kind == .ram
            ? sidewaysClient.group(forSocket: socket, kind: .writeProtect)
            : nil
        let hideGroup = sidewaysClient.group(forSocket: socket, kind: .hide)
        return RowContent(
            slot: "\(socket.priority)",
            title: displayName(for: socket),
            status: statusText(for: socket),
            imageName: socket.imageName,
            writeGroup: writeGroup,
            hideGroup: hideGroup,
            tooltip: tooltip(for: socket))
    }

    /// The display fields shared by the OS row and the sideways socket rows, so
    /// the two are one view and cannot drift. A memory row shows one title line
    /// and never a name beneath it.
    struct RowContent {
        var slot: String
        var title: String
        var status: String
        var imageName: String
        var writeGroup: SidewaysClient.ProtectionGroup?
        var hideGroup: SidewaysClient.ProtectionGroup?
        var tooltip: String
    }

    /// One memory row -- slot column, title, write-protect padlock column,
    /// status, actions menu -- rendered the same whether the content came from a
    /// socket or from the OS row.
    private func memoryRow(_ content: RowContent) -> some View {
        HStack(spacing: 8) {
            // Slot column (a socket's priority; blank for the OS row). Tight
            // monospaced column so it lines up across rows.
            Text(content.slot)
                .font(.caption.monospacedDigit())
                .foregroundColor(.secondary)
                .frame(width: 18, alignment: .trailing)

            // ROM title (with version when known), else filename, else blank
            // for empty slots - the "-" status column already signals empty.
            Text(content.title)
                .font(.subheadline)
                .lineLimit(1)
                .truncationMode(.middle)
                .foregroundColor(.primary)
                .frame(maxWidth: .infinity, alignment: .leading)

            // Write-protect padlock in a fixed-width leading column, so the
            // status column stays aligned whether or not a row has the control.
            // Shown only for a RAM slot covered by a write-protect group; other
            // rows (and the OS row) get an equal-width spacer.
            Group {
                if let writeGroup = content.writeGroup {
                    writeProtectPadlock(group: writeGroup)
                } else {
                    Color.clear
                }
            }
            .frame(width: 16)

            // ROM/RAM/- status, mirroring the running device.
            Text(content.status)
                .font(.caption.monospaced())
                .foregroundColor(.secondary)
                .frame(width: 32, alignment: .trailing)

            // Always render the menu so populated and empty rows have the exact
            // same trailing column width; hide and disable it on rows with
            // nothing to act on. Trying to substitute a bare Image as a
            // placeholder dances the status column because the Menu's internal
            // padding doesn't match a plain Image. A row covered by a protection
            // group is always actionable (the menu carries Write-Protect / Hide)
            // even with no image for Copy Path / reveal.
            let actionable = !content.imageName.isEmpty
                || content.writeGroup != nil || content.hideGroup != nil
            actionsMenu(imageName: content.imageName,
                        writeGroup: content.writeGroup, hideGroup: content.hideGroup)
                .opacity(actionable ? 1 : 0)
                .allowsHitTesting(actionable)
                .accessibilityHidden(!actionable)
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 4)
        // Whole-row tooltip (welcome here, unlike on the actions button): the
        // socket label (e.g. "IC68 (slot 11)", "SRAM W", "IC71") and slots, or
        // the OS title and server path -- keeping the sidebar compact while
        // making the detail discoverable on hover.
        .help(content.tooltip)
        .contentShape(Rectangle())
    }

    private func tooltip(for socket: SidewaysClient.Socket) -> String {
        var lines: [String] = []
        lines.append(socket.label)
        let slotWord = socket.slots.count > 1 ? "Slots" : "Slot"
        let slotList = socket.slots.map(String.init).joined(separator: ", ")
        lines.append("\(slotWord) \(slotList)  (priority \(socket.priority))")
        if let header = socket.romHeader {
            // Display kinds as "Language / Service / ROMFS" - matches the
            // header parser's output set, capitalised for readability.
            let displayKinds = header.kinds.map { kind -> String in
                kind == "romfs" ? "ROMFS" : kind.capitalized
            }
            if !displayKinds.isEmpty {
                lines.append(displayKinds.joined(separator: " / "))
            }
        }
        if !socket.imageName.isEmpty {
            lines.append(socket.imageName)
        }
        return lines.joined(separator: "\n")
    }

    // MARK: - Display helpers

    private func displayName(for socket: SidewaysClient.Socket) -> String {
        if let header = socket.romHeader {
            if !header.version.isEmpty {
                return "\(header.title) \(header.version)"
            }
            return header.title
        }
        if socket.populated && !socket.imageName.isEmpty {
            return URL(fileURLWithPath: socket.imageName).lastPathComponent
        }
        return ""
    }

    private func statusText(for socket: SidewaysClient.Socket) -> String {
        switch socket.kind {
        case .rom:   return "ROM"
        case .ram:   return "RAM"
        case .empty: return "\u{2014}"  // em dash
        }
    }

    // MARK: - Write-protect padlock

    /// A single clickable padlock reflecting a write-protect group's switch. It
    /// is a Button, not a Toggle: the glyph mirrors the server-reported group
    /// state and updates only from the RPC re-fetch, never optimistically on the
    /// click, so it honours feedback_state_vs_action_controls as one control.
    private func writeProtectPadlock(group: SidewaysClient.ProtectionGroup) -> some View {
        Button {
            toggle(group: group, kind: .writeProtect, engaged: !group.writeProtected)
        } label: {
            Image(systemName: group.writeProtected ? "lock.fill" : "lock.open")
                .font(.caption)
                .foregroundColor(group.writeProtected ? .accentColor : .secondary)
        }
        .buttonStyle(.borderless)
        .help(group.writeProtected
              ? "Allow writes to this sideways RAM"
              : "Write-protect this sideways RAM")
    }

    private func toggle(group: SidewaysClient.ProtectionGroup,
                        kind: SidewaysClient.ProtectionKind, engaged: Bool) {
        Task { await sidewaysClient.setSlotProtection(groupID: group.id, kind: kind, engaged: engaged) }
    }

    // MARK: - Actions menu (Write-Protect / Hide + read-only Copy Path / Reveal)

    private func actionsMenu(imageName: String,
                             writeGroup: SidewaysClient.ProtectionGroup?,
                             hideGroup: SidewaysClient.ProtectionGroup?) -> some View {
        Menu {
            // Checkmarked protection items, the idiomatic macOS menu form. Each
            // follows its group's server-reported state and toggles the whole
            // group. Write-Protect mirrors the padlock; Hide has no padlock.
            if let writeGroup {
                Toggle("Write-Protect", isOn: Binding(
                    get: { writeGroup.writeProtected },
                    set: { toggle(group: writeGroup, kind: .writeProtect, engaged: $0) }
                ))
            }
            if let hideGroup {
                Toggle("Hide", isOn: Binding(
                    get: { hideGroup.hidden },
                    set: { toggle(group: hideGroup, kind: .hide, engaged: $0) }
                ))
            }
            if writeGroup != nil || hideGroup != nil {
                Divider()
            }

            Button("Copy Path") {
                NSPasteboard.general.clearContents()
                NSPasteboard.general.setString(imageName, forType: .string)
            }
            .disabled(imageName.isEmpty)

            // Both conditions matter, and the file check alone is not
            // enough. It asks whether the path exists on *this* host while
            // the path came from the server's, so against a remote server
            // with a similar install layout it can succeed and reveal a
            // different file that merely shares a path -- worse than not
            // offering the command at all.
            if isServerLocal && isExistingFile(imageName) {
                Button("Reveal in Finder") {
                    NSWorkspace.shared.activateFileViewerSelecting(
                        [URL(fileURLWithPath: imageName)])
                }
            }
        } label: {
            Image(systemName: "ellipsis.circle")
                .foregroundColor(.secondary)
        }
        .menuStyle(.borderlessButton)
        // The disclosure chevron Menu renders by default would offset the
        // trailing column on rows that have a menu versus those that
        // don't, dancing the ROM/RAM status text out of alignment. The
        // ellipsis already signals "menu available"; the chevron is
        // redundant.
        .menuIndicator(.hidden)
        .fixedSize()
        // No tooltip on the menu button: it would appear as the menu opens and
        // confuse itself with the whole-row tooltip. Row tooltips stay.
    }

    private func isExistingFile(_ path: String) -> Bool {
        FileManager.default.fileExists(atPath: path)
    }

    // MARK: - Loading / empty states

    private var loadingView: some View {
        HStack(spacing: 8) {
            ProgressView().scaleEffect(0.7)
            Text("Reading sideways state\u{2026}")
                .font(.caption)
                .foregroundColor(.secondary)
        }
        .padding(12)
    }

    private var noSidewaysView: some View {
        Text("This machine has no sideways memory.")
            .font(.caption)
            .foregroundColor(.secondary)
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(12)
    }
}
