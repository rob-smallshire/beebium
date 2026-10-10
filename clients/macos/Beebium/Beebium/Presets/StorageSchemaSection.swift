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

import Foundation

/// Storage section schema from `describe-preset-schema` CLI command.
///
/// Describes the storage capabilities of a machine model including
/// FDC socket options (Model B) and floppy drive configuration.
struct StorageSchemaSection: Codable {
    let type: String  // "storage"
    /// Fixed fittings of the model: a cassette port, and a built-in floppy disc
    /// controller on models that have one soldered in (e.g. the B+). A model
    /// with no built-in FDC instead offers an `fdc_socket` the user can fit a
    /// controller into.
    let builtin: StorageBuiltin?
    let fdcSocket: FDCSocketSchema?
    let floppyDrives: [FloppyDriveSchema]?

    enum CodingKeys: String, CodingKey {
        case type
        case builtin
        case fdcSocket = "fdc_socket"
        case floppyDrives = "floppy_drives"
    }

    /// Whether this model has an active floppy disc controller given `draft`:
    /// a soldered-in controller, or a controller fitted into the FDC socket.
    /// When false there is nothing for floppy drives to attach to, so the
    /// Storage section shows none -- matching the live Storage sidebar.
    func hasActiveFdc(in draft: MachineDraft) -> Bool {
        if builtin?.fdc != nil { return true }
        guard let fdcSocket = fdcSocket else { return false }
        let selectedId = draft.storage?.fdcSocket?.id ?? "none"
        // The empty socket's option carries no device; any real controller does.
        return fdcSocket.options.first(where: { $0.id == selectedId })?.device != nil
    }
}

/// A model's fixed storage fittings, as reported by the schema's `builtin` block.
struct StorageBuiltin: Codable {
    let cassette: Bool?
    /// The soldered-in floppy disc controller, if the model has one; nil when
    /// the model instead offers a user-fittable FDC socket (or has no FDC).
    let fdc: BuiltinFdc?
}

/// A soldered-in floppy disc controller described by the schema.
struct BuiltinFdc: Codable {
    let device: String?
    let label: String?
}

/// Schema for the FDC socket (Model B only).
///
/// The FDC socket allows different disc controllers to be installed.
struct FDCSocketSchema: Codable {
    let options: [FDCOption]
}

/// An available FDC option that can be installed in the FDC socket.
struct FDCOption: Codable, Identifiable, Hashable {
    let id: String
    let label: String
    let device: String?
    let description: String?
}

/// Schema for a floppy drive slot.
struct FloppyDriveSchema: Codable {
    /// Drive number (0, 1, 2, or 3)
    let drive: Int
    /// Supported track counts (e.g., [40, 80])
    let tracks: [Int]
    /// Number of sides/heads (1 or 2)
    let sides: Int
    /// Supported image file types (e.g., ["ssd", "dsd", "adf"])
    let imageTypes: [String]

    enum CodingKeys: String, CodingKey {
        case drive, tracks, sides
        case imageTypes = "image_types"
    }
}
