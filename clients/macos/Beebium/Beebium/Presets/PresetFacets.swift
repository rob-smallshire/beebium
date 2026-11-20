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

/// A `list-presets` row's `facets` object. An exclusive facet's value is a single
/// string (e.g. `"disc_controller": "1770"`, `"None"` when absent); a set facet's
/// is an array (e.g. `"storage": ["Floppy","Cassette"]`, possibly empty). Both are
/// normalised to a list of values per key.
struct PresetFacetValues: Decodable, Equatable, Sendable {
    let byKey: [String: [String]]

    private struct DynamicKey: CodingKey {
        let stringValue: String
        var intValue: Int? { nil }
        init?(stringValue: String) { self.stringValue = stringValue }
        init?(intValue: Int) { nil }
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: DynamicKey.self)
        var result: [String: [String]] = [:]
        for key in container.allKeys {
            if let array = try? container.decode([String].self, forKey: key) {
                result[key.stringValue] = array
            } else if let single = try? container.decode(String.self, forKey: key) {
                result[key.stringValue] = [single]
            }
        }
        byKey = result
    }

    init(_ byKey: [String: [String]]) { self.byKey = byKey }
}
