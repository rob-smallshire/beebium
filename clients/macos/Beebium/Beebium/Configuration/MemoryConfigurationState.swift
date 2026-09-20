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

/// Observable state for the Memory (sideways ROM/RAM) configuration tab.
///
/// Built from the machine's sideways_bank schema plus the selected preset, it
/// presents one row per physical socket, ordered by boot priority (highest
/// slot first, mirroring the MOS scan). Each socket's initial content is the
/// preset's assignment if any, otherwise the machine's default ROM, otherwise
/// empty. The user can override a socket; only changed sockets emit
/// `--sideways` at launch, layering over the preset.
class MemoryConfigurationState: ObservableObject {

    /// Whether a socket is empty, holds a ROM, or is sideways RAM. This is
    /// orthogonal to whatever image is loaded: a ROM is read-only, RAM is
    /// writable, but either can be populated from an image at startup.
    enum SocketKind: String, CaseIterable, Identifiable, Equatable {
        case empty
        case rom
        case ram

        var id: String { rawValue }

        var label: String {
            switch self {
            case .empty: return "Empty"
            case .rom:   return "ROM"
            case .ram:   return "RAM"
            }
        }
    }

    /// A socket's assignment: its kind plus an optional image. A ROM needs an
    /// image; sideways RAM may optionally be pre-loaded from one at startup
    /// (battery-backed RAM appears pre-loaded to the machine); empty has none.
    struct SocketContent: Equatable {
        var kind: SocketKind
        var image: String?  // ROM image, or RAM pre-load image; nil for empty/blank RAM
        /// Power-on write-protect position for a sideways-RAM socket. Meaningful
        /// only when `kind == .ram`; emitted as `--write-protect <slot>` at
        /// launch. (Presets do not yet persist this - see the save-as-preset
        /// TODO in the plan.)
        var writeProtected: Bool

        static let empty = SocketContent(kind: .empty, image: nil)

        init(kind: SocketKind, image: String? = nil, writeProtected: Bool = false) {
            self.kind = kind
            self.image = image
            self.writeProtected = writeProtected
        }

        /// Build from a preset slot's type/image strings.
        init(presetType: String, image: String?) {
            switch presetType {
            case "rom": self.init(kind: .rom, image: image)
            case "ram": self.init(kind: .ram, image: image)
            default:    self.init(kind: .empty, image: nil)
            }
        }

        /// Human-readable summary for the row.
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

    /// One configurable socket row.
    struct SocketConfig: Identifiable {
        let id: String          // socket label, e.g. "IC52"
        let label: String
        let slots: [Int]        // logical slot numbers wired to this socket
        let priority: Int       // = max(slots); higher wins at boot
        let supportsRom: Bool
        let supportsRam: Bool
        let supportsEmpty: Bool
        /// The socket has a RAM write-protect switch (see the schema). Gates the
        /// config-time write-protect checkbox and its launch argument.
        let supportsWriteProtect: Bool
        /// Slot the initial content occupies (from preset or default), if any.
        let sourceSlot: Int?
        /// Content before any edit (for display and revert).
        let initialContent: SocketContent
        /// Current (possibly edited) content.
        var content: SocketContent
        /// Parsed ROM title/version for the loaded image, if it is a recognised
        /// sideways ROM (resolved asynchronously via describe-rom).
        var resolvedTitle: String? = nil
        var resolvedVersion: String? = nil
        /// What the ROM is - any combination of "language", "service", "romfs".
        /// Empty until resolved (or for an unrecognised image).
        var resolvedKinds: [String] = []

        var isChanged: Bool { content != initialContent }

        /// What to show for the contents. The kind (ROM/RAM/Empty) is shown by
        /// the adjacent control, so this describes the loaded image: a recognised
        /// ROM's title (and version) when known, otherwise the filename - the
        /// same for a ROM or a pre-loaded sideways-RAM image. An Empty socket
        /// always reads "Empty" regardless of any image retained in the model
        /// (so switching kind back to ROM/RAM restores the title cleanly).
        var displayText: String {
            guard content.kind != .empty,
                  let image = content.image, !image.isEmpty else {
                return content.displayLabel
            }
            if let titled = romTitleAndVersion { return titled }
            return URL(fileURLWithPath: image).lastPathComponent
        }

        private var romTitleAndVersion: String? {
            guard let title = resolvedTitle, !title.isEmpty else { return nil }
            if let version = resolvedVersion, !version.isEmpty { return "\(title) \(version)" }
            return title
        }

        /// Slot used when emitting `--sideways` for this socket. Using the
        /// slot the initial content occupied keeps the server's per-slot
        /// override aligned with the preset and avoids aliased-socket conflicts;
        /// for a previously empty socket the highest (priority) slot is used.
        var emitSlot: Int { sourceSlot ?? priority }

        /// Comma-separated slot numbers for display, e.g. "0, 4, 8, 12".
        var slotsLabel: String { slots.map(String.init).joined(separator: ", ") }
    }

    @Published var sockets: [SocketConfig] = []

    /// Rebuild rows from the machine schema and the preset's sideways assignments.
    func configure(schema: SidewaysSchemaSection, presetSlots: [PresetSidewaysSlot]) {
        var rows: [SocketConfig] = []
        for socket in schema.sockets {
            let presetSlot = presetSlots.first { socket.slots.contains($0.slot) }
            let defaultRom = schema.defaultRoms.first { socket.slots.contains($0.slot) }

            let content: SocketContent
            let sourceSlot: Int?
            if let presetSlot = presetSlot {
                content = SocketContent(presetType: presetSlot.type, image: presetSlot.imageUri)
                sourceSlot = presetSlot.slot
            } else if let defaultRom = defaultRom {
                content = SocketContent(kind: .rom, image: defaultRom.image)
                sourceSlot = defaultRom.slot
            } else if !socket.supportsEmpty && socket.supportsRam && !socket.supportsRom {
                // Fixed-RAM socket (e.g. B+ 128K's SRAM W/X/Y/Z): there is
                // no "empty" or "rom" state available; the socket is always
                // RAM. Reflect that in the initial content so the kind
                // picker has a valid selection to display.
                content = SocketContent(kind: .ram, image: nil)
                sourceSlot = nil
            } else {
                content = .empty
                sourceSlot = nil
            }

            rows.append(SocketConfig(
                id: socket.label,
                label: socket.label,
                slots: socket.slots,
                priority: socket.priority,
                supportsRom: socket.supportsRom,
                supportsRam: socket.supportsRam,
                supportsEmpty: socket.supportsEmpty,
                supportsWriteProtect: socket.supportsWriteProtect,
                sourceSlot: sourceSlot,
                initialContent: content,
                content: content))
        }
        // Highest priority (highest slot) first, mirroring the boot scan order.
        sockets = rows.sorted { $0.priority > $1.priority }
    }

    /// True if any socket has been changed from its preset/default content.
    var hasChanges: Bool { sockets.contains { $0.isChanged } }

    /// Reset every socket to its initial (preset/default) content.
    func revertAll() {
        for index in sockets.indices {
            sockets[index].content = sockets[index].initialContent
        }
    }

    /// Reorder *images* between the fixed sockets, leaving each socket's
    /// identity (label/slots/priority) and hardware-fixed kind in place.
    ///
    /// Drag-to-reorder is fundamentally about priority - the user wants ROM
    /// X to sit at a higher-priority slot than ROM Y. A reorder shouldn't
    /// also try to mutate slot kinds, because some sockets have a fixed
    /// hardware kind: the B+ 128K's SRAM W/X/Y/Z banks are forever RAM,
    /// not configurable.
    ///
    /// Move semantics:
    ///   1. The image (and its resolved title/version/kinds) follows the
    ///      drag through the post-move row order. The drag carries the
    ///      *originating* kind alongside it, used only as a hint.
    ///   2. Each socket's kind is then adjusted just enough to make the
    ///      assignment legal:
    ///        - A socket receiving an image while currently Empty bumps to
    ///          the source's kind if it can (RAM or ROM), else to whichever
    ///          ROM/RAM the socket supports.
    ///        - A socket losing its image becomes Empty if the hardware
    ///          allows; otherwise (e.g. fixed-RAM bank) it stays its
    ///          current kind with image cleared (blank RAM).
    ///        - Any other case leaves the socket's kind unchanged.
    func moveContents(fromOffsets source: IndexSet, toOffset destination: Int) {
        struct Payload {
            // Kind of the socket the image came from - a hint for the
            // destination when it has to bump out of Empty.
            var sourceKind: SocketKind
            var image: String?
            var resolvedTitle: String?
            var resolvedVersion: String?
            var resolvedKinds: [String]
        }
        var payloads = sockets.map { socket in
            Payload(
                sourceKind: socket.content.kind,
                image: socket.content.image,
                resolvedTitle: socket.resolvedTitle,
                resolvedVersion: socket.resolvedVersion,
                resolvedKinds: socket.resolvedKinds
            )
        }
        payloads.move(fromOffsets: source, toOffset: destination)

        for index in sockets.indices {
            let payload = payloads[index]
            let socket = sockets[index]
            let currentKind = socket.content.kind
            let hasImage = !(payload.image ?? "").isEmpty

            let newKind: SocketKind
            if hasImage {
                if currentKind == .empty {
                    // Bump out of Empty to host the image. Prefer the kind
                    // the image originated as; fall back if the destination
                    // hardware can't be that kind.
                    if payload.sourceKind == .ram, socket.supportsRam {
                        newKind = .ram
                    } else if payload.sourceKind == .rom, socket.supportsRom {
                        newKind = .rom
                    } else if socket.supportsRom {
                        newKind = .rom
                    } else if socket.supportsRam {
                        newKind = .ram
                    } else {
                        newKind = currentKind  // unreachable in practice
                    }
                } else {
                    newKind = currentKind
                }
            } else {
                // No image arriving. Collapse to Empty if the socket can.
                if currentKind != .empty, socket.supportsEmpty {
                    newKind = .empty
                } else {
                    newKind = currentKind  // e.g. fixed-RAM stays RAM, blanked
                }
            }

            // Write-protect is the socket's own switch, not a property of the
            // image being dragged, so it stays with the socket (cleared if the
            // socket is no longer RAM). Rebuilding content wholesale would
            // otherwise silently untick it on every reorder.
            let keepWriteProtect = (newKind == .ram) ? socket.content.writeProtected : false
            sockets[index].content = SocketContent(
                kind: newKind, image: payload.image, writeProtected: keepWriteProtect
            )
            sockets[index].resolvedTitle = payload.resolvedTitle
            sockets[index].resolvedVersion = payload.resolvedVersion
            sockets[index].resolvedKinds = payload.resolvedKinds
        }
    }

    /// `--sideways` arguments for the sockets the user changed. Unchanged
    /// sockets emit nothing, so the preset's own sideways_bank still applies;
    /// changed sockets override per slot via the server's merge.
    func sidewaysLaunchArguments() -> [String] {
        var arguments: [String] = []
        for socket in sockets where socket.isChanged {
            let slot = socket.emitSlot
            let image = socket.content.image
            switch socket.content.kind {
            case .empty:
                arguments.append(contentsOf: ["--sideways", "\(slot):empty"])
            case .rom:
                // A ROM needs an image; skip an incomplete selection so the
                // preset/default for the slot still applies.
                if let image = image, !image.isEmpty {
                    arguments.append(contentsOf: ["--sideways", "\(slot):rom:\(image)"])
                }
            case .ram:
                if let image = image, !image.isEmpty {
                    arguments.append(contentsOf: ["--sideways", "\(slot):ram:\(image)"])
                } else {
                    arguments.append(contentsOf: ["--sideways", "\(slot):ram"])
                }
            }
            // Set the power-on write-protect position, but only where the board
            // has the switch and the socket is RAM. The server rejects the flag
            // for any other socket, so match its rule rather than emit and fail.
            if socket.supportsWriteProtect && socket.content.kind == .ram
                && socket.content.writeProtected {
                arguments.append(contentsOf: ["--write-protect", "\(slot)"])
            }
        }
        return arguments
    }

    // MARK: - ROM title resolution

    /// Resolves a ROM image reference to its parsed header (runs describe-rom),
    /// supplied by the host. Main-actor isolated so it can call PresetManager.
    var headerResolver: (@MainActor (String) async -> RomHeaderInfo?)?

    /// Resolve titles for every socket that holds an image.
    @MainActor
    func resolveTitles() async {
        for index in sockets.indices {
            await resolveTitle(at: index)
        }
    }

    /// Resolve (or clear) the title/version for one socket's loaded image.
    @MainActor
    func resolveTitle(at index: Int) async {
        guard sockets.indices.contains(index) else { return }
        guard let resolver = headerResolver,
              let image = sockets[index].content.image, !image.isEmpty else {
            sockets[index].resolvedTitle = nil
            sockets[index].resolvedVersion = nil
            sockets[index].resolvedKinds = []
            return
        }
        let info = await resolver(image)
        guard sockets.indices.contains(index) else { return }
        if let info = info, info.recognised, !info.title.isEmpty {
            sockets[index].resolvedTitle = info.title
            sockets[index].resolvedVersion = info.version
            sockets[index].resolvedKinds = info.kinds
        } else {
            sockets[index].resolvedTitle = nil
            sockets[index].resolvedVersion = nil
            sockets[index].resolvedKinds = []
        }
    }
}
