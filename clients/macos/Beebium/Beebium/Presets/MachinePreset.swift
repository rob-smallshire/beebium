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

/// A machine preset representing a configuration that can be used to launch an emulator.
///
/// System presets are auto-discovered from preset files in the presets/ directory.
/// User presets are custom configurations created by users and can be edited.
struct MachinePreset: Identifiable, Hashable {
    /// The origin of a preset.
    enum Source: Hashable {
        /// System-provided preset from presets/*.preset.beebium
        case systemPreset
        /// User-created preset from ~/Library/Application Support/...
        case userPreset
    }

    let id: UUID
    /// The CLI preset ID (slug derived from name, e.g., "bbc-model-b-with-acorn-dfs")
    let presetId: String
    var name: String
    let coreExecutablePath: String
    /// Path to the preset file on disk
    let presetFilepath: String
    let source: Source
    /// The preset's own `machine_name` template, if it declares one (e.g.
    /// "Station {econet-station} (AUN, Model B)"). nil when the preset has none,
    /// in which case the launcher builds a template from the display name.
    let machineName: String?
    let modelName: String
    let modelDescription: String?
    /// Release date for sorting, format: YYYY, YYYY-MM, or YYYY-MM-DD
    let releaseDate: String?
    var configuration: [String: String]

    /// Whether this is a system-provided preset (not user-created).
    var isSystemProvided: Bool { source == .systemPreset }

    /// Whether this preset can be edited by the user.
    var isEditable: Bool { source == .userPreset }

    static func == (lhs: MachinePreset, rhs: MachinePreset) -> Bool {
        lhs.id == rhs.id
    }

    func hash(into hasher: inout Hasher) {
        hasher.combine(id)
    }
}
