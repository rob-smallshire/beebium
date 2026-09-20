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

/// Sideways ROM/RAM bank section from the `describe-preset-schema` CLI command.
///
/// Describes the machine's physical sideways sockets and the ROMs it installs
/// by default. Frontends use this to present a socket grid (the Memory tab)
/// that reflects the real hardware, including the Model B's address aliasing.
struct SidewaysSchemaSection: Codable {
    let type: String  // "sideways_bank"
    let hasAliasing: Bool
    let sockets: [SidewaysSocketSchema]
    let defaultRoms: [SidewaysDefaultRom]

    enum CodingKeys: String, CodingKey {
        case type
        case hasAliasing = "has_aliasing"
        case sockets
        case defaultRoms = "default_roms"
    }
}

/// One physical sideways socket.
///
/// On a stock Model B, partial address decoding makes a single socket answer
/// four slot numbers (e.g. IC52 -> 0, 4, 8, 12); on a ROM/RAM board each socket
/// is wired to one slot. The MOS scans slots high to low at boot, so a socket's
/// effective priority is the highest slot number it answers.
struct SidewaysSocketSchema: Codable, Identifiable {
    let label: String           // e.g. "IC52" or "Slot 7"
    let slots: [Int]            // logical slot numbers wired to this socket
    let capabilities: [String]  // any of "rom", "ram", "empty", "write_protect"
    let runtimeConfigurable: Bool

    var id: String { label }

    /// Effective boot priority: the highest slot number the socket answers.
    /// A higher number wins the language-ROM selection at reset.
    var priority: Int { slots.max() ?? 0 }

    /// Slot number used to address this socket on the CLI (`--sideways`). Any
    /// wired slot resolves to the same socket; the highest matches the priority.
    var representativeSlot: Int { slots.max() ?? (slots.first ?? 0) }

    var supportsRom: Bool { capabilities.contains("rom") }
    var supportsRam: Bool { capabilities.contains("ram") }
    var supportsEmpty: Bool { capabilities.contains("empty") }
    /// The socket has a RAM write-protect switch: only then may the write-protect
    /// control be offered (and only while the slot is RAM). Absent for sideways
    /// RAM with no such switch, e.g. the B+ 128K SRAM banks.
    var supportsWriteProtect: Bool { capabilities.contains("write_protect") }

    enum CodingKeys: String, CodingKey {
        case label, slots, capabilities
        case runtimeConfigurable = "runtime_configurable"
    }
}

/// A ROM the machine installs by default unless a preset or `--sideways`
/// overrides the slot.
struct SidewaysDefaultRom: Codable {
    let slot: Int
    let image: String
    let role: String  // e.g. "language", "filing"
}
