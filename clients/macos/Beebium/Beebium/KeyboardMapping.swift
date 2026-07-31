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

/// A key mapping entry from JSON with nested platform-specific structure.
///
/// The macOS client only understands the "macOS" and "bbc" sections.
/// All other platform sections (windows, linux, etc.) are preserved for round-tripping.
///
/// Example JSON:
/// {
///   "macOS": { "keyCode": 111, "option": true },
///   "windows": { "VKCode": 123, "alt": true },  // Preserved but not interpreted
///   "bbc": { "keyName": "Break" }
/// }
struct KeyMappingEntry {
    /// All key/value pairs from JSON (preserves unknown platform sections)
    var rawJSON: [String: Any]

    /// macOS platform section (interpreted by this client)
    var macOS: MacOSKeySpec? {
        guard let dict = rawJSON["macOS"] as? [String: Any] else { return nil }
        return MacOSKeySpec(dict: dict)
    }

    /// BBC target section (interpreted by this client)
    var bbc: BBCKeySpec? {
        guard let dict = rawJSON["bbc"] as? [String: Any] else { return nil }
        return BBCKeySpec(dict: dict)
    }

    struct MacOSKeySpec {
        let keyCode: UInt16
        let shift: Bool
        let control: Bool
        let option: Bool
        let command: Bool
        let function: Bool

        init?(dict: [String: Any]) {
            guard let keyCode = dict["keyCode"] as? Int else { return nil }
            self.keyCode = UInt16(keyCode)
            self.shift = dict["shift"] as? Bool ?? false
            self.control = dict["control"] as? Bool ?? false
            self.option = dict["option"] as? Bool ?? false
            self.command = dict["command"] as? Bool ?? false
            self.function = dict["function"] as? Bool ?? false
        }

        /// Convert to NSEvent.ModifierFlags
        var requiredModifiers: NSEvent.ModifierFlags {
            var flags: NSEvent.ModifierFlags = []
            if shift { flags.insert(.shift) }
            if control { flags.insert(.control) }
            if option { flags.insert(.option) }
            if command { flags.insert(.command) }
            if function { flags.insert(.function) }
            return flags
        }
    }

    struct BBCKeySpec {
        let keyName: String
        let shift: Bool
        let ctrl: Bool
        let disabled: Bool?  // Tri-state: nil = not disableable, false/true = disableable with default state
        let action: String?  // Optional action description for reference table

        init?(dict: [String: Any]) {
            guard let keyName = dict["keyName"] as? String else { return nil }
            self.keyName = keyName
            self.shift = dict["shift"] as? Bool ?? false
            self.ctrl = dict["ctrl"] as? Bool ?? false
            self.disabled = dict["disabled"] as? Bool  // nil if absent
            self.action = dict["action"] as? String  // nil if absent
        }
    }

    /// Create from JSON dictionary (preserves all sections)
    init(rawJSON: [String: Any]) {
        self.rawJSON = rawJSON
    }
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

    /// All entries from JSON (for round-trip serialization)
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

    /// Create macOS section dictionary from keyCode and modifiers
    private static func createMacOSDict(keyCode: UInt16, modifiers: NSEvent.ModifierFlags) -> [String: Any] {
        var dict: [String: Any] = ["keyCode": Int(keyCode)]
        if modifiers.contains(.shift) { dict["shift"] = true }
        if modifiers.contains(.control) { dict["control"] = true }
        if modifiers.contains(.option) { dict["option"] = true }
        if modifiers.contains(.command) { dict["command"] = true }
        if modifiers.contains(.function) { dict["function"] = true }
        return dict
    }

    /// Create BBC section dictionary from key name and modifiers
    private static func createBBCDict(keyName: String, shift: Bool, ctrl: Bool, disabled: Bool? = nil, action: String? = nil) -> [String: Any] {
        var dict: [String: Any] = ["keyName": keyName]
        if shift { dict["shift"] = true }
        if ctrl { dict["ctrl"] = true }
        if let disabled = disabled { dict["disabled"] = disabled }
        if let action = action { dict["action"] = action }
        return dict
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
                let macOSDict = Self.createMacOSDict(keyCode: keyCode, modifiers: ref.requireModifiers)
                let bbcDict = Self.createBBCDict(keyName: ref.bbcKeyName, shift: ref.bbcWithShift, ctrl: ref.bbcWithCtrl)
                let rawJSON: [String: Any] = [
                    "macOS": macOSDict,
                    "bbc": bbcDict
                ]
                entries.append(KeyMappingEntry(rawJSON: rawJSON))
            }
        }
        self.allEntries = entries
    }

    /// Load from JSON dictionary, extracting macOS entries while preserving all for round-trip
    /// - Parameters:
    ///   - json: The JSON dictionary
    ///   - isBuiltIn: Whether this is a built-in mapping (default: false for user mappings)
    init(json: [String: Any], isBuiltIn: Bool = false) {
        // Extract known fields
        if let idString = json["id"] as? String,
           let uuid = UUID(uuidString: idString) {
            self.id = uuid
        } else {
            self.id = UUID()
        }

        self.name = json["name"] as? String ?? "Unnamed"
        self.isBuiltIn = isBuiltIn
        self.characterMapping = json["characterMapping"] as? Bool ?? true
        self.synchronizeCapsLock = json["synchronizeCapsLock"] as? Bool ?? true

        // Parse all entries, preserving raw JSON
        let entriesJSON = json["keyMappings"] as? [[String: Any]] ?? []
        self.allEntries = entriesJSON.map { KeyMappingEntry(rawJSON: $0) }

        // Build keyMappings: [UInt16: [BBCKeyRef]] from entries
        var mappingDict: [UInt16: [BBCKeyRef]] = [:]

        for entry in allEntries {
            // Extract platform-specific key code (macOS in this case)
            guard let macOSSpec = entry.macOS,
                  let bbcSpec = entry.bbc else {
                // Skip entries without macOS or BBC sections
                continue
            }

            let ref = BBCKeyRef(
                bbcKeyName: bbcSpec.keyName,
                bbcWithShift: bbcSpec.shift,
                bbcWithCtrl: bbcSpec.ctrl,
                requireModifiers: macOSSpec.requiredModifiers
            )

            let keyCode = macOSSpec.keyCode

            // Append to array for this keyCode
            if mappingDict[keyCode] == nil {
                mappingDict[keyCode] = []
            }
            mappingDict[keyCode]?.append(ref)
        }

        self.keyMappings = mappingDict

        // Build disableableKeys from entries with "disabled" field
        var disableableDict: [String: Bool] = [:]
        for entry in allEntries {
            guard let bbcSpec = entry.bbc,
                  let disabledState = bbcSpec.disabled else {
                continue
            }
            // First entry wins if multiple mappings target same key
            if disableableDict[bbcSpec.keyName] == nil {
                disableableDict[bbcSpec.keyName] = disabledState
            }
        }
        self.disableableKeys = disableableDict
    }

    /// Serialize to JSON dictionary, preserving all platform entries
    func toJSON() -> [String: Any] {
        return [
            "version": 1,
            "id": id.uuidString,
            "name": name,
            "characterMapping": characterMapping,
            "synchronizeCapsLock": synchronizeCapsLock,
            "keyMappings": allEntries.map { $0.rawJSON }
        ]
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
        if keyMappings[keyCode] == nil {
            keyMappings[keyCode] = []
        }
        keyMappings[keyCode]?.append(ref)

        // Update allEntries
        let macOSDict = Self.createMacOSDict(keyCode: keyCode, modifiers: requireModifiers)
        let bbcDict = Self.createBBCDict(keyName: bbcKeyName, shift: bbcWithShift, ctrl: bbcWithCtrl)
        let rawJSON: [String: Any] = [
            "macOS": macOSDict,
            "bbc": bbcDict
        ]
        allEntries.append(KeyMappingEntry(rawJSON: rawJSON))
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

