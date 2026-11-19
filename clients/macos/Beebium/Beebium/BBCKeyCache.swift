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

/// Cached BBC keyboard mappings fetched from the core at startup.
///
/// This is the single source of truth for BBC key positions. The cache is populated
/// from the core's `GetAllKeyMappings()` gRPC call and provides lookups by character
/// or key name.
final class BBCKeyCache {

    /// A cached BBC key mapping entry
    struct Entry {
        /// Internal key number: (row << 4) | column
        let ikNumber: UInt8

        /// Whether BBC Shift is required for this key
        let needsShift: Bool

        /// Human-readable key name (e.g., "A", "Return", "f0", "Up")
        let name: String

        /// Row in the BBC keyboard matrix (0-7)
        var row: UInt8 { (ikNumber >> 4) & 0x0F }

        /// Column in the BBC keyboard matrix (0-9)
        var column: UInt8 { ikNumber & 0x0F }
    }

    /// Lookup by character (for logical/character mapping)
    private var byCharacter: [Character: Entry] = [:]

    /// Lookup by key name (for physical mapping and named keys)
    /// Keys are stored lowercase for case-insensitive lookup
    private var byName: [String: Entry] = [:]

    /// Whether the cache has been populated
    private(set) var isLoaded: Bool = false

    /// Number of entries in the cache
    var count: Int { byName.count }

    /// Load the cache from a GetAllKeyMappings() response
    func load(from response: Beebium_AllKeyMappingsResponse) {
        byCharacter.removeAll()
        byName.removeAll()

        for mapping in response.mappings {
            let entry = Entry(
                ikNumber: UInt8(mapping.ikNumber & 0xFF),
                needsShift: mapping.needsShift,
                name: mapping.name
            )

            // Add to name lookup (case-insensitive)
            byName[mapping.name.lowercased()] = entry

            // Add to character lookup if this is a character-producing key
            if !mapping.character.isEmpty,
               let char = mapping.character.first {
                byCharacter[char] = entry
            }
        }

        isLoaded = true
        print("[BBCKeyCache] Loaded \(byName.count) key mappings")
    }

    /// Look up a BBC key by the character it produces
    /// - Parameter character: The character to look up
    /// - Returns: The BBC key entry, or nil if not found
    func lookup(character: Character) -> Entry? {
        return byCharacter[character]
    }

    /// Look up a BBC key by its name (case-insensitive)
    /// - Parameter name: The key name (e.g., "Up", "f0", "Return")
    /// - Returns: The BBC key entry, or nil if not found
    func lookup(name: String) -> Entry? {
        return byName[name.lowercased()]
    }

    /// Whether the connected machine has a BBC key by this name, for section 3.6
    /// applicability (docs/keyboard-mapping-files.md): an entry whose key the
    /// machine lacks does not apply, so the resolver skips it and the keyboard
    /// view omits it. Break is not a matrix key and counts as present on every
    /// machine. Until the catalogue is loaded the machine is unknown, so every
    /// key counts as present rather than every entry vanishing.
    func machineHasKey(named name: String) -> Bool {
        guard isLoaded else { return true }
        if name == "Break" { return true }
        return lookup(name: name) != nil
    }

    /// Get all character mappings (for debugging/diagnostics)
    var allCharacters: [Character: Entry] {
        return byCharacter
    }

    /// Get all name mappings (for debugging/diagnostics)
    var allNames: [String: Entry] {
        return byName
    }
}
