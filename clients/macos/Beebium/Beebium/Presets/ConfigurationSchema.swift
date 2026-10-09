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

// The preset file itself is modelled by `MachineDraft` (Presets/MachineDraft.swift),
// which the dialog edits and discovery reads for its metadata. There is one
// preset-document representation, not a separate read-only parse here.
