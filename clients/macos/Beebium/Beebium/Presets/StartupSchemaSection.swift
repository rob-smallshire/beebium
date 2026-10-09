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

/// The `startup` section of describe-preset-schema: the startup keyboard links,
/// expressed either as a screen mode plus auto-boot, or as the whole links byte
/// (the two forms are mutually exclusive).
struct StartupSchemaSection: Codable {
    let type: String   // "startup"
    let screenMode: IntRange
    let autoBoot: BoolDefault
    let links: IntRange

    struct IntRange: Codable {
        let min: Int
        let max: Int
        /// Present for screen_mode; absent for links.
        let `default`: Int?
    }

    struct BoolDefault: Codable {
        let `default`: Bool
    }

    enum CodingKeys: String, CodingKey {
        case type, links
        case screenMode = "screen_mode"
        case autoBoot = "auto_boot"
    }
}
