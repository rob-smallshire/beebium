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

/// Turns the server's `list-presets` rows into the system and user preset
/// lists the UI shows. Each row is paired with the executable that owns its
/// model -- `beebium-<model>` under the servers directory, or the listing
/// executable for a row that names no model -- which later schema, resolve and
/// launch calls go through. A row whose owning executable is not present in
/// this build is skipped, being a preset the app cannot run.
enum PresetCatalog {
    struct Result: Equatable {
        var system: [MachinePreset] = []
        var user: [MachinePreset] = []
        /// Ids of rows dropped because their owning executable is absent.
        var skipped: [String] = []
    }

    /// Map `rows` to presets. Pure given its two lookups, so it is testable
    /// without running a server:
    /// - `executableExists`: whether a server executable path is runnable.
    /// - `modelDisplayName`: the model's human display name (e.g. "BBC Model
    ///   B") for a row, "" when it cannot be determined. Shown only as the
    ///   "Based on" label; the preset's own `name` comes from the row.
    static func build(
        rows: [PresetListing],
        serversDirpath: String,
        listingExecutablePath: String,
        executableExists: (String) -> Bool,
        modelDisplayName: (PresetListing) -> String
    ) -> Result {
        var result = Result()
        for row in rows {
            let executablePath = row.model.map { "\(serversDirpath)/beebium-\($0)" } ?? listingExecutablePath
            guard executableExists(executablePath) else {
                result.skipped.append(row.id)
                continue
            }
            let preset = MachinePreset(
                id: UUID(),
                presetId: row.id,
                name: row.name,
                coreExecutablePath: executablePath,
                presetFilepath: row.filepath,
                source: row.source == "user" ? .userPreset : .systemPreset,
                machineDescription: row.machineDescription,
                modelName: modelDisplayName(row),
                modelDescription: row.description,
                releaseDate: row.releaseDate,
                hasThumbnail: row.thumbnail
            )
            if preset.source == .userPreset {
                result.user.append(preset)
            } else {
                result.system.append(preset)
            }
        }
        return result
    }
}
