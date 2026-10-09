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

import Foundation

/// Projects a machine's sideways-bank schema and a draft's slot assignments into
/// the per-socket rows the Memory section shows, and writes edits straight back
/// into the draft's slots.
///
/// The draft is the single source of truth: this type stores nothing of its own
/// (the Memory view holds only transient UI state -- a drag in progress and the
/// ROM-title cache). A row's current content is derived from the draft's slots
/// each time; an edit produces a new slots array, which the view assigns back to
/// the draft. Every edited socket is written as an explicit slot, so a draft is
/// an exact record of the configuration the user saw and does not change meaning
/// if a server default changes later.
///
/// Until `resolve-preset` becomes the oracle (and fills the model's default ROMs
/// into the draft as explicit slots), a socket the draft does not mention falls
/// back to the schema's default ROM for display, so the Memory tab still shows
/// BASIC in the top slot. That fallback is display-only and never written.
struct SidewaysSocketEditor {
    let schema: SidewaysSchemaSection

    /// Whether a socket is empty, holds a ROM, or is sideways RAM. Orthogonal to
    /// any image loaded: a ROM is read-only, RAM is writable, either can be
    /// populated from an image at startup.
    enum SocketKind: String, CaseIterable, Identifiable, Equatable {
        case empty, rom, ram
        var id: String { rawValue }
        var label: String {
            switch self {
            case .empty: return "Empty"
            case .rom:   return "ROM"
            case .ram:   return "RAM"
            }
        }
    }

    /// A socket's assignment: kind, optional image, and (RAM only) the power-on
    /// write-protect switch position.
    struct Content: Equatable {
        var kind: SocketKind
        var image: String?      // ROM image, or RAM pre-load image; nil for empty/blank RAM
        var writeProtected: Bool

        static let empty = Content(kind: .empty, image: nil, writeProtected: false)

        init(kind: SocketKind, image: String? = nil, writeProtected: Bool = false) {
            self.kind = kind
            self.image = image
            self.writeProtected = writeProtected
        }

        /// Build from a draft slot's type/image/write-protect.
        init(slot: MachineDraft.SidewaysSlot) {
            switch slot.type {
            case "rom": self.init(kind: .rom, image: slot.imageUri)
            case "ram": self.init(kind: .ram, image: slot.imageUri,
                                  writeProtected: slot.writeProtected ?? false)
            default:    self.init(kind: .empty, image: nil)
            }
        }

        /// Human-readable summary for the row's contents (the kind is shown by
        /// the adjacent control, so this describes the image).
        var displayLabel: String {
            switch kind {
            case .empty:
                return "Empty"
            case .rom:
                guard let image = image, !image.isEmpty else { return "ROM (no image)" }
                return URL(fileURLWithPath: image).lastPathComponent
            case .ram:
                guard let image = image, !image.isEmpty else { return "Sideways RAM" }
                return "Sideways RAM (preload: \(URL(fileURLWithPath: image).lastPathComponent))"
            }
        }

        /// The image path if it is an existing file on disk, for Reveal in
        /// Finder. Bare ROM-library names (from presets/defaults) return nil.
        var revealableFilepath: String? {
            guard let image = image, image.contains("/"),
                  FileManager.default.fileExists(atPath: image) else { return nil }
            return image
        }
    }

    /// One socket row, derived from the schema and the draft.
    struct Row: Identifiable {
        let socket: SidewaysSocketSchema
        /// Current content, from the draft (or the schema default ROM fallback).
        var content: Content
        /// Whether the current content differs from the original preset's.
        var isChanged: Bool

        var id: String { socket.label }
        var label: String { socket.label }
        var slots: [Int] { socket.slots }
        var priority: Int { socket.priority }
        var supportsRom: Bool { socket.supportsRom }
        var supportsRam: Bool { socket.supportsRam }
        var supportsEmpty: Bool { socket.supportsEmpty }
        var supportsWriteProtect: Bool { socket.supportsWriteProtect }

        /// Comma-separated slot numbers for display, e.g. "0, 4, 8, 12".
        var slotsLabel: String { slots.map(String.init).joined(separator: ", ") }
    }

    // MARK: - Projection

    /// The socket rows, highest boot-priority first, with each socket's current
    /// content from `currentSlots` and its changed-ness measured against
    /// `originalSlots` (the preset as loaded).
    func rows(currentSlots: [MachineDraft.SidewaysSlot],
              originalSlots: [MachineDraft.SidewaysSlot]) -> [Row] {
        schema.sockets
            .map { socket in
                let current = content(for: socket, in: currentSlots)
                let original = content(for: socket, in: originalSlots)
                return Row(socket: socket, content: current, isChanged: current != original)
            }
            .sorted { $0.priority > $1.priority }
    }

    /// The content a socket shows for a given slots array: the draft's slot for
    /// the socket if any, else the schema's default ROM, else fixed RAM (a
    /// RAM-only socket) or empty.
    func content(for socket: SidewaysSocketSchema,
                 in slots: [MachineDraft.SidewaysSlot]) -> Content {
        if let slot = slots.first(where: { socket.slots.contains($0.slot) }) {
            return Content(slot: slot)
        }
        if let defaultRom = schema.defaultRoms.first(where: { socket.slots.contains($0.slot) }) {
            return Content(kind: .rom, image: defaultRom.image)
        }
        if !socket.supportsEmpty && socket.supportsRam && !socket.supportsRom {
            // Fixed-RAM socket (e.g. B+ 128K's SRAM W/X/Y/Z): always RAM.
            return Content(kind: .ram, image: nil)
        }
        return .empty
    }

    // MARK: - Write-through

    /// The slots array with `socket` set to `content`, written as one explicit
    /// slot entry (any existing entries for the socket's slot numbers removed).
    ///
    /// An incomplete ROM selection (ROM with no image) records no override -- the
    /// server would reject it -- so the socket's entries are simply removed; the
    /// Memory view only ever sets a ROM content once an image has been chosen.
    func slots(settingSocket socket: SidewaysSocketSchema,
               to content: Content,
               in slots: [MachineDraft.SidewaysSlot]) -> [MachineDraft.SidewaysSlot] {
        let slotNumber = preferredSlot(for: socket, in: slots)
        var result = slots.filter { !socket.slots.contains($0.slot) }

        switch content.kind {
        case .empty:
            result.append(.init(slot: slotNumber, type: "empty"))
        case .rom:
            guard let image = content.image, !image.isEmpty else { break }
            result.append(.init(slot: slotNumber, type: "rom", imageUri: image))
        case .ram:
            let writeProtected = (socket.supportsWriteProtect && content.writeProtected) ? true : nil
            result.append(.init(slot: slotNumber, type: "ram",
                                imageUri: content.image?.isEmpty == true ? nil : content.image,
                                writeProtected: writeProtected))
        }
        return result.sorted { $0.slot < $1.slot }
    }

    /// Reorder the *images* between sockets (highest-priority first), leaving each
    /// socket's identity and hardware-fixed kind in place, and return the new
    /// slots. Mirrors the drag-to-reorder semantics of the live Memory list.
    func slots(movingContentsFromOffsets source: IndexSet,
               toOffset destination: Int,
               currentSlots: [MachineDraft.SidewaysSlot],
               originalSlots: [MachineDraft.SidewaysSlot]) -> [MachineDraft.SidewaysSlot] {
        let orderedRows = rows(currentSlots: currentSlots, originalSlots: originalSlots)

        struct Payload {
            var sourceKind: SocketKind
            var image: String?
        }
        var payloads = orderedRows.map { Payload(sourceKind: $0.content.kind, image: $0.content.image) }
        payloads.move(fromOffsets: source, toOffset: destination)

        var result = currentSlots
        for (index, row) in orderedRows.enumerated() {
            let payload = payloads[index]
            let socket = row.socket
            let currentKind = row.content.kind
            let hasImage = !(payload.image ?? "").isEmpty

            let newKind: SocketKind
            if hasImage {
                if currentKind == .empty {
                    if payload.sourceKind == .ram, socket.supportsRam {
                        newKind = .ram
                    } else if payload.sourceKind == .rom, socket.supportsRom {
                        newKind = .rom
                    } else if socket.supportsRom {
                        newKind = .rom
                    } else if socket.supportsRam {
                        newKind = .ram
                    } else {
                        newKind = currentKind
                    }
                } else {
                    newKind = currentKind
                }
            } else {
                if currentKind != .empty, socket.supportsEmpty {
                    newKind = .empty
                } else {
                    newKind = currentKind  // e.g. fixed-RAM stays RAM, blanked
                }
            }

            // Write-protect stays with the socket (cleared if it is no longer RAM).
            let keepWriteProtect = (newKind == .ram) ? row.content.writeProtected : false
            let newContent = Content(kind: newKind, image: payload.image, writeProtected: keepWriteProtect)
            result = slots(settingSocket: socket, to: newContent, in: result)
        }
        return result
    }

    // MARK: - Helpers

    /// The slot number to write an explicit entry at for a socket: an existing
    /// draft entry's slot, else the default ROM's slot, else the socket's
    /// representative (highest) slot. Keeping the number the content already
    /// occupied avoids aliased-socket surprises.
    private func preferredSlot(for socket: SidewaysSocketSchema,
                               in slots: [MachineDraft.SidewaysSlot]) -> Int {
        if let existing = slots.first(where: { socket.slots.contains($0.slot) }) {
            return existing.slot
        }
        if let defaultRom = schema.defaultRoms.first(where: { socket.slots.contains($0.slot) }) {
            return defaultRom.slot
        }
        return socket.representativeSlot
    }
}
