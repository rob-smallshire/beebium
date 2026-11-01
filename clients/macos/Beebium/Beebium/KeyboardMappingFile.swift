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

// The on-disk model of a keyboard mapping file: parsing and serialisation only,
// kept separate from the resolution logic in KeyboardMapping. This split (issue
// #231) lets the version 2 cross-client format and the version 1 -> 2 conversion
// land here without disturbing how a resolved key is computed.

/// A single key mapping entry as it appears in the file, with its platform
/// sections preserved verbatim.
///
/// The macOS client interprets the `macOS` and `bbc` sections; every other
/// platform section (`windows`, `linux`, ...) is kept untouched for round
/// tripping so a file authored for another client survives being read and
/// written here.
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

    /// Build an entry from a macOS key code, host modifiers and a BBC target.
    /// This is the one place that composes the `macOS` and `bbc` sections, so a
    /// programmatically built or user-edited mapping serialises identically to a
    /// loaded one.
    static func make(
        macOSKeyCode: UInt16,
        modifiers: NSEvent.ModifierFlags,
        bbcKeyName: String,
        bbcShift: Bool,
        bbcCtrl: Bool,
        disabled: Bool? = nil,
        action: String? = nil
    ) -> KeyMappingEntry {
        var macOSDict: [String: Any] = ["keyCode": Int(macOSKeyCode)]
        if modifiers.contains(.shift) { macOSDict["shift"] = true }
        if modifiers.contains(.control) { macOSDict["control"] = true }
        if modifiers.contains(.option) { macOSDict["option"] = true }
        if modifiers.contains(.command) { macOSDict["command"] = true }
        if modifiers.contains(.function) { macOSDict["function"] = true }

        var bbcDict: [String: Any] = ["keyName": bbcKeyName]
        if bbcShift { bbcDict["shift"] = true }
        if bbcCtrl { bbcDict["ctrl"] = true }
        if let disabled = disabled { bbcDict["disabled"] = disabled }
        if let action = action { bbcDict["action"] = action }

        return KeyMappingEntry(rawJSON: ["macOS": macOSDict, "bbc": bbcDict])
    }
}

/// A parsed keyboard mapping file: the format fields and the raw entries,
/// without any resolution logic. KeyboardMapping builds its resolver indices
/// from one of these; it is also what writes the file back out.
struct KeyboardMappingFile {
    var version: Int
    var id: UUID
    var name: String
    var characterMapping: Bool
    var synchronizeCapsLock: Bool
    var entries: [KeyMappingEntry]

    /// Parse a mapping file's top-level JSON. Unknown top-level fields are not
    /// modelled here; the fields below match the version 1 format exactly, so a
    /// version 1 file round-trips unchanged.
    init(json: [String: Any]) {
        if let idString = json["id"] as? String,
           let uuid = UUID(uuidString: idString) {
            self.id = uuid
        } else {
            self.id = UUID()
        }

        self.version = json["version"] as? Int ?? 1
        self.name = json["name"] as? String ?? "Unnamed"
        self.characterMapping = json["characterMapping"] as? Bool ?? true
        self.synchronizeCapsLock = json["synchronizeCapsLock"] as? Bool ?? true

        let entriesJSON = json["keyMappings"] as? [[String: Any]] ?? []
        self.entries = entriesJSON.map { KeyMappingEntry(rawJSON: $0) }
    }

    /// Build a file model directly from known fields (serialisation side).
    init(
        version: Int = 1,
        id: UUID,
        name: String,
        characterMapping: Bool,
        synchronizeCapsLock: Bool,
        entries: [KeyMappingEntry]
    ) {
        self.version = version
        self.id = id
        self.name = name
        self.characterMapping = characterMapping
        self.synchronizeCapsLock = synchronizeCapsLock
        self.entries = entries
    }

    /// Serialise to a JSON dictionary, preserving every entry's platform
    /// sections. The shape matches the version 1 format.
    func toJSON() -> [String: Any] {
        return [
            "version": version,
            "id": id.uuidString,
            "name": name,
            "characterMapping": characterMapping,
            "synchronizeCapsLock": synchronizeCapsLock,
            "keyMappings": entries.map { $0.rawJSON }
        ]
    }
}
