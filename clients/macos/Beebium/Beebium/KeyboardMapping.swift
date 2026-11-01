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
import AppKit

// This file owns resolution: turning a host key event into a BBC key. The
// on-disk file model (parsing, serialisation, the raw entries) lives in
// KeyboardMappingFile.swift; a KeyboardMapping is built from one of those and
// writes itself back through one (issue #231).

/// A reference to a BBC key with required host modifiers and BBC modifier overrides.
struct BBCKeyRef {
    /// The BBC key name (e.g., "Up", "f0", "a", "Return")
    let bbcKeyName: String

    /// Override BBC Shift state (false = don't press, true = press)
    let bbcWithShift: Bool

    /// Override BBC Ctrl state (false = don't press, true = press)
    let bbcWithCtrl: Bool

    /// Required host modifiers for this mapping to match
    let requireModifiers: NSEvent.ModifierFlags
}

/// A keyboard mapping that translates host key input to BBC Micro keys.
///
/// Each mapping has:
/// - `characterMapping`: Whether to use character→BBC key resolution
/// - `keyMappings`: Direct keyCode→BBC key mappings (checked first)
///
/// Resolution order:
/// 1. Check `keyMappings` for the keyCode
/// 2. If not found and `characterMapping` is true, look up the character in the cache
final class KeyboardMapping: Identifiable {

    /// Unique identifier for this mapping
    let id: UUID

    /// Human-readable name (e.g., "Logical (Default)", "Thrust")
    var name: String

    /// Whether this mapping is built-in (read-only)
    let isBuiltIn: Bool

    /// Enable character→BBC key resolution
    var characterMapping: Bool

    /// Whether to synchronize macOS Caps Lock state with BBC Caps Lock
    var synchronizeCapsLock: Bool

    /// Direct keyCode→BBC key mappings (checked first)
    /// Extracted from allEntries for macOS use
    /// Maps keyCode to array of possible mappings (supports multiple mappings with different modifiers)
    var keyMappings: [UInt16: [BBCKeyRef]]

    /// All entries from the file (for round-trip serialization)
    /// Includes entries for other platforms that we don't understand
    internal private(set) var allEntries: [KeyMappingEntry]

    /// Maps BBC key names to their default disabled state
    /// Key present in dictionary = disableable (value = default state)
    /// Key absent = NOT disableable
    var disableableKeys: [String: Bool] = [:]

    /// The result of resolving an input to a BBC key
    struct ResolvedKey {
        /// BBC key name (e.g., "A", "Return", "Break")
        let bbcKeyName: String

        /// Internal key number: (row << 4) | column
        /// Note: For Break key, this is 0 (not a matrix key)
        let ikNumber: UInt8

        /// Whether BBC Shift should be pressed
        let bbcShift: Bool

        /// Whether BBC Ctrl should be pressed
        let bbcCtrl: Bool

        /// True iff this resolution came from the character-mapping path
        /// (cache lookup). The KeyboardClient uses this to distinguish
        /// "user is typing a character" from "user pressed a directly
        /// mapped physical key" so it can decide BBC SHIFT correctly.
        let fromCharacterMapping: Bool

        /// Whether this is the Break key (requires special handling)
        var isBreak: Bool { bbcKeyName == "Break" }
    }

    /// Resolve a key input to a BBC key using this mapping
    /// - Parameters:
    ///   - input: The key input event
    ///   - cache: The BBC key cache for character/name lookups
    /// - Returns: The resolved BBC key, or nil if not mapped
    func resolve(_ input: KeyInput, cache: BBCKeyCache) -> ResolvedKey? {
        // 1. Try physical keyCode mapping first
        if let candidates = keyMappings[input.keyCode] {
            // Filter input modifiers to relevant ones (exclude Caps Lock)
            let relevantModifiers = input.modifiers.intersection([
                .shift, .control, .option, .command, .function
            ])

            // Find best matching candidate using priority rules
            var exactMatch: BBCKeyRef?
            var supersetMatch: BBCKeyRef?
            var noRequirementMatch: BBCKeyRef?

            for candidate in candidates {
                let required = candidate.requireModifiers

                if required.isEmpty {
                    // No requirements - always matches (fallback)
                    noRequirementMatch = candidate
                } else if relevantModifiers == required {
                    // Exact match - highest priority
                    exactMatch = candidate
                    break  // Stop searching, this is the best
                } else if relevantModifiers.isSuperset(of: required) {
                    // Superset match - middle priority
                    if supersetMatch == nil {
                        supersetMatch = candidate
                    }
                }
                // If modifiers don't contain all required, candidate is rejected
            }

            // Select best match
            let match = exactMatch ?? supersetMatch ?? noRequirementMatch

            if let ref = match {
                return resolveRef(ref, cache: cache)
            }
        }

        // 2. Fall back to character mapping if enabled. The lookup character
        // ignores CTRL, which names a control code rather than a key.
        if characterMapping,
           let char = input.lookupCharacter,
           let entry = cache.lookup(character: char) {
            return ResolvedKey(
                bbcKeyName: entry.name,
                ikNumber: entry.ikNumber,
                bbcShift: entry.needsShift,
                bbcCtrl: false,
                fromCharacterMapping: true
            )
        }

        return nil
    }

    /// Helper to resolve a BBCKeyRef to a ResolvedKey
    private func resolveRef(_ ref: BBCKeyRef, cache: BBCKeyCache) -> ResolvedKey? {
        // Break key is not in the matrix - handle specially
        if ref.bbcKeyName == "Break" {
            return ResolvedKey(
                bbcKeyName: "Break",
                ikNumber: 0,  // Not a matrix key
                bbcShift: ref.bbcWithShift,
                bbcCtrl: ref.bbcWithCtrl,
                fromCharacterMapping: false
            )
        }

        // Normal matrix key - look up in cache for ikNumber only.
        guard let entry = cache.lookup(name: ref.bbcKeyName) else {
            return nil
        }

        // BBC SHIFT / CTRL come solely from the explicit ref.bbcWithShift /
        // bbcWithCtrl flags. We deliberately ignore entry.needsShift here:
        // the cache's case-insensitive byName index collides between
        // letter-pair entries (e.g. lowercase 'a' and uppercase 'A' share
        // ikNumber 0x41 and lowercase to "a"), so the lookup is not a
        // reliable source of "does this matrix position need shift to
        // produce its shifted face". A direct keyMapping author who wants
        // the shifted face spells it out via shift=true in the JSON.
        return ResolvedKey(
            bbcKeyName: entry.name,
            ikNumber: entry.ikNumber,
            bbcShift: ref.bbcWithShift,
            bbcCtrl: ref.bbcWithCtrl,
            fromCharacterMapping: false
        )
    }

    /// Create a built-in mapping programmatically
    init(
        id: UUID = UUID(),
        name: String,
        isBuiltIn: Bool = false,
        characterMapping: Bool = true,
        synchronizeCapsLock: Bool = true,
        keyMappings: [UInt16: [BBCKeyRef]] = [:]
    ) {
        self.id = id
        self.name = name
        self.isBuiltIn = isBuiltIn
        self.characterMapping = characterMapping
        self.synchronizeCapsLock = synchronizeCapsLock
        self.keyMappings = keyMappings

        // Build allEntries from keyMappings
        var entries: [KeyMappingEntry] = []
        for (keyCode, refs) in keyMappings {
            for ref in refs {
                entries.append(KeyMappingEntry.make(
                    macOSKeyCode: keyCode,
                    modifiers: ref.requireModifiers,
                    bbcKeyName: ref.bbcKeyName,
                    bbcShift: ref.bbcWithShift,
                    bbcCtrl: ref.bbcWithCtrl
                ))
            }
        }
        self.allEntries = entries
    }

    /// Load from a mapping file, extracting the macOS entries into the resolver
    /// indices while preserving all entries for round-trip.
    /// - Parameters:
    ///   - json: The mapping file's JSON dictionary
    ///   - isBuiltIn: Whether this is a built-in mapping (default: false for user mappings)
    init(json: [String: Any], isBuiltIn: Bool = false) {
        let file = KeyboardMappingFile(json: json)

        self.id = file.id
        self.name = file.name
        self.isBuiltIn = isBuiltIn
        self.characterMapping = file.characterMapping
        self.synchronizeCapsLock = file.synchronizeCapsLock
        self.allEntries = file.entries

        self.keyMappings = Self.directMappings(from: file.entries)
        self.disableableKeys = Self.disableableKeys(from: file.entries)
    }

    /// Build the keyCode -> [BBCKeyRef] resolver index from file entries,
    /// keeping only those with both a macOS and a BBC section.
    private static func directMappings(from entries: [KeyMappingEntry]) -> [UInt16: [BBCKeyRef]] {
        var mappingDict: [UInt16: [BBCKeyRef]] = [:]
        for entry in entries {
            guard let macOSSpec = entry.macOS,
                  let bbcSpec = entry.bbc else {
                continue
            }

            let ref = BBCKeyRef(
                bbcKeyName: bbcSpec.keyName,
                bbcWithShift: bbcSpec.shift,
                bbcWithCtrl: bbcSpec.ctrl,
                requireModifiers: macOSSpec.requiredModifiers
            )

            mappingDict[macOSSpec.keyCode, default: []].append(ref)
        }
        return mappingDict
    }

    /// Build the disableable-keys index from file entries. The first entry for a
    /// given BBC key wins if several target the same key.
    private static func disableableKeys(from entries: [KeyMappingEntry]) -> [String: Bool] {
        var disableableDict: [String: Bool] = [:]
        for entry in entries {
            guard let bbcSpec = entry.bbc,
                  let disabledState = bbcSpec.disabled else {
                continue
            }
            if disableableDict[bbcSpec.keyName] == nil {
                disableableDict[bbcSpec.keyName] = disabledState
            }
        }
        return disableableDict
    }

    /// Serialize to a JSON dictionary, preserving all platform entries.
    func toJSON() -> [String: Any] {
        return KeyboardMappingFile(
            id: id,
            name: name,
            characterMapping: characterMapping,
            synchronizeCapsLock: synchronizeCapsLock,
            entries: allEntries
        ).toJSON()
    }

    /// Clone this mapping with a new name
    func clone(newName: String) -> KeyboardMapping {
        let cloned = KeyboardMapping(json: toJSON())
        // Generate new ID for the clone
        return KeyboardMapping(
            id: UUID(),
            name: newName,
            isBuiltIn: false,
            characterMapping: cloned.characterMapping,
            synchronizeCapsLock: cloned.synchronizeCapsLock,
            keyMappings: cloned.keyMappings
        )
    }

    /// Add or update a key mapping
    /// - Parameters:
    ///   - keyCode: The macOS virtual key code
    ///   - bbcKeyName: The BBC key name to map to
    ///   - bbcWithShift: Whether to press BBC Shift
    ///   - bbcWithCtrl: Whether to press BBC Ctrl
    ///   - requireModifiers: Required host modifiers for this mapping to match
    func setKeyMapping(
        keyCode: UInt16,
        bbcKeyName: String,
        bbcWithShift: Bool = false,
        bbcWithCtrl: Bool = false,
        requireModifiers: NSEvent.ModifierFlags = []
    ) {
        guard !isBuiltIn else { return }

        let ref = BBCKeyRef(
            bbcKeyName: bbcKeyName,
            bbcWithShift: bbcWithShift,
            bbcWithCtrl: bbcWithCtrl,
            requireModifiers: requireModifiers
        )

        // Append to array
        keyMappings[keyCode, default: []].append(ref)

        // Update allEntries
        allEntries.append(KeyMappingEntry.make(
            macOSKeyCode: keyCode,
            modifiers: requireModifiers,
            bbcKeyName: bbcKeyName,
            bbcShift: bbcWithShift,
            bbcCtrl: bbcWithCtrl
        ))
    }

    /// Remove all key mappings for a given keyCode
    func removeKeyMapping(keyCode: UInt16) {
        guard !isBuiltIn else { return }

        keyMappings.removeValue(forKey: keyCode)
        allEntries.removeAll { entry in
            entry.macOS?.keyCode == keyCode
        }
    }
}
