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

/// One row of the server's `list-presets --format jsonl` output: its
/// authoritative account of a stored preset. The server owns what presets
/// exist and what they are called, so the client reads this rather than
/// opening preset files itself.
///
/// `id`, `name` and `source` are always present. `name` is the display name,
/// with the server substituting the model's own display name when a preset
/// declares none, so the client needs no fallback of its own. `model`,
/// `machineDescription`, `description` and `releaseDate` appear only when the
/// preset gives them, never as null. `machineDescription` is the preset's
/// description template (the window subtitle), distinct from `description`, the
/// picker text. `filepath` is the stored preset's path, to hand back to `start
/// --preset`, `resolve-preset` and reveal-in-Finder. `thumbnail` says whether
/// `show-preset --thumbnail` has one to write.
struct PresetListing: Decodable, Equatable {
    let id: String
    let name: String
    /// "system" or "user".
    let source: String
    let model: String?
    let machineDescription: String?
    let description: String?
    let releaseDate: String?
    let filepath: String
    let thumbnail: Bool

    enum CodingKeys: String, CodingKey {
        case id
        case name
        case source
        case model
        case machineDescription = "machine_description"
        case description
        case releaseDate = "release_date"
        case filepath
        case thumbnail
    }
}
