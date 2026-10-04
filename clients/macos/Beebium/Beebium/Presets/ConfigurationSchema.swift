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

/// Root schema returned by `describe-preset-schema` CLI.
///
/// Contains machine identification and configuration sections that describe
/// the machine's capabilities (storage, networking, coprocessor, etc.).
struct PresetSchema: Codable {
    let schemaVersion: Int
    let model: ModelInfo
    let sections: [SchemaSection]

    enum CodingKeys: String, CodingKey {
        case schemaVersion = "schema_version"
        case model, sections
    }
}

/// Machine identification information.
struct ModelInfo: Codable {
    let id: String
    let name: String
    let description: String
}

/// A configuration section describing a domain of machine capabilities.
///
/// The `type` field identifies the section kind (e.g., "storage", "coprocessor").
/// Additional fields vary by section type and can be decoded on-demand.
struct SchemaSection: Codable {
    let type: String
    // Additional fields (builtin, fdc_socket, floppy_drives, etc.) are
    // decoded on-demand when building configuration UI for that section.
}

/// Data parsed from a preset file (*.preset.beebium).
///
/// Preset files declare the target model and optional metadata.
/// The `model` field is required and maps to executable names (beebium-{model}).
struct PresetFileData: Codable {
    /// The model ID that maps to an executable (e.g., "model-b" -> beebium-model-b)
    let model: String
    /// Optional display name (if absent, use name from executable's schema)
    let name: String?
    /// Optional name template (e.g. "Station {econet-station} (AUN, Model B)").
    /// The server treats `--machine-name` and this as templates; the app reads
    /// it so a launched machine inherits the preset author's placeholders.
    let machineName: String?
    /// Optional description (if absent, use description from executable's schema)
    let description: String?
    /// Release date for sorting, format: YYYY, YYYY-MM, or YYYY-MM-DD
    let releaseDate: String?
    /// Sideways ROM/RAM slots the preset configures (absent if none)
    let sidewaysBank: PresetSidewaysBank?

    enum CodingKeys: String, CodingKey {
        case model, name, description
        case machineName = "machine_name"
        case releaseDate = "release_date"
        case sidewaysBank = "sideways_bank"
    }
}

/// Sideways slot assignments declared by a preset file's `sideways_bank` section.
struct PresetSidewaysBank: Codable {
    let slots: [PresetSidewaysSlot]
}

/// One slot assignment in a preset's `sideways_bank`.
struct PresetSidewaysSlot: Codable {
    let slot: Int
    let type: String        // "rom", "ram", or "empty"
    let imageUri: String?   // ROM/RAM image reference (absent for empty/blank RAM)

    enum CodingKeys: String, CodingKey {
        case slot, type
        case imageUri = "image_uri"
    }
}
