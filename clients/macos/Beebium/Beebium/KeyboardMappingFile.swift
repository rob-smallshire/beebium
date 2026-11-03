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

// The on-disk model of a keyboard mapping file: parsing, version 1 -> version 2
// conversion, and serialisation. Resolution (turning a host key event into a BBC
// key) lives in KeyboardMapping.swift. See docs/keyboard-mapping-files.md for the
// normative format; this is the macOS client's reader and writer of it (#231).
//
// Version 2 is the cross-client format: entries are keyed on W3C UI Events
// `code` values (a key by position, the same on every host), with per-platform
// sections only for keys that have no neutral code. The client reads version 2,
// converts a version 1 file on first load, and writes version 2 only. A key with
// no `code` on this host, and the macOS `function` modifier, stay macOS platform
// entries.

/// The macOS-facing view of an entry for the resolver: a physical key code and
/// the host modifiers that must be held. Built by translating a version 2 entry
/// (a `code` plus `with`, or a `platform: macOS` native entry) to macOS terms.
struct MacOSKeySpec {
    let keyCode: UInt16
    let requiredModifiers: NSEvent.ModifierFlags
}

/// The BBC side of an entry.
struct BBCKeySpec {
    let key: String
    let shift: Bool
    let ctrl: Bool
    let disabled: Bool?  // Tri-state: nil = not disableable, false/true = disableable with default state
    let action: String?  // Optional action description for reference table

    init?(dict: [String: Any]) {
        guard let key = dict["key"] as? String else { return nil }
        self.key = key
        self.shift = dict["shift"] as? Bool ?? false
        self.ctrl = dict["ctrl"] as? Bool ?? false
        self.disabled = dict["disabled"] as? Bool  // nil if absent
        self.action = dict["action"] as? String  // nil if absent
    }
}

/// Mapping between the format's host-modifier names and NSEvent.ModifierFlags.
/// `Meta` is the Command key on macOS; `Alt` is Option. The Caps Lock state is
/// never a host modifier here.
enum HostModifierName {
    static func flags(from names: [String]) -> NSEvent.ModifierFlags {
        var flags: NSEvent.ModifierFlags = []
        for name in names {
            switch name {
            case "Shift": flags.insert(.shift)
            case "Control": flags.insert(.control)
            case "Alt": flags.insert(.option)
            case "Meta": flags.insert(.command)
            default: break  // unknown names are ignored for resolution, preserved on disk
            }
        }
        return flags
    }

    /// The `with` names for host modifier flags, in the format's canonical order.
    /// `.function` has no neutral name and is handled by a platform entry, so it
    /// is not produced here.
    static func names(from flags: NSEvent.ModifierFlags) -> [String] {
        var names: [String] = []
        if flags.contains(.shift) { names.append("Shift") }
        if flags.contains(.control) { names.append("Control") }
        if flags.contains(.option) { names.append("Alt") }
        if flags.contains(.command) { names.append("Meta") }
        return names
    }
}

/// A single version 2 key mapping entry, preserving its JSON verbatim so that a
/// foreign-platform entry, or a field this client does not model, survives being
/// read and written. The macOS client interprets `code`/`with` entries and
/// `platform: macOS` entries; it keeps every other entry untouched.
struct KeyMappingEntry {
    /// The entry's JSON, preserved for round tripping.
    var rawJSON: [String: Any]

    init(rawJSON: [String: Any]) {
        self.rawJSON = rawJSON
    }

    /// The W3C `code`, if this is a portable entry.
    var code: String? { rawJSON["code"] as? String }

    /// The host modifiers required, from `with`.
    var withModifiers: [String] { rawJSON["with"] as? [String] ?? [] }

    /// The platform of a platform-specific entry (`macOS`, `linux`, `windows`, ...).
    var platform: String? { rawJSON["platform"] as? String }

    /// The BBC side, or nil if absent.
    var bbc: BBCKeySpec? {
        guard let dict = rawJSON["bbc"] as? [String: Any] else { return nil }
        return BBCKeySpec(dict: dict)
    }

    /// This host's platform name, as the format's `when.platform` predicate and
    /// 3.4 `platform` entries name it.
    static let hostPlatform = "macOS"

    /// Whether this entry's `when` condition holds on this host, so the entry
    /// takes part in resolution, the keyboard view's labelled keys and editing.
    /// An entry with no `when` applies everywhere; an empty `when` object holds
    /// everywhere too. Each predicate must hold; a predicate this client does not
    /// know -- an unrecognised name, or a value whose shape it does not describe
    /// -- does not hold, so the entry does not apply. It is still kept untouched
    /// on write, exactly as another platform's 3.4 entry is
    /// (docs/keyboard-mapping-files.md section 3.5).
    var appliesToThisHost: Bool {
        guard let whenValue = rawJSON["when"] else { return true }
        // A `when` of any shape other than an object is one this client does not
        // understand, so no predicate can be read to hold: the entry does not apply.
        guard let when = whenValue as? [String: Any] else { return false }
        for (predicate, value) in when {
            switch predicate {
            case "platform":
                guard Self.platformPredicateHolds(value) else { return false }
            default:
                // An unknown predicate does not hold.
                return false
            }
        }
        return true
    }

    /// Whether a `when.platform` value -- a single name or an array of names --
    /// names this host. A value of any other shape is one this client does not
    /// describe and does not hold.
    private static func platformPredicateHolds(_ value: Any) -> Bool {
        if let name = value as? String {
            return name == hostPlatform
        }
        if let names = value as? [String] {
            return names.contains(hostPlatform)
        }
        return false
    }

    /// The macOS resolver view of this entry, or nil if it does not apply to this
    /// host: a portable `code` entry whose code this host has, or a
    /// `platform: macOS` native entry, whose `when` (if any) also holds here.
    /// Entries for other platforms, codes this host lacks, and entries a `when`
    /// excludes return nil (and are preserved untouched on disk).
    var macOS: MacOSKeySpec? {
        guard appliesToThisHost else { return nil }
        if let code = code {
            guard let keyCode = W3CKeyCode.macKeyCode(forCode: code) else { return nil }
            return MacOSKeySpec(
                keyCode: keyCode,
                requiredModifiers: HostModifierName.flags(from: withModifiers))
        }
        if platform == "macOS", let keyCodeInt = rawJSON["keyCode"] as? Int {
            var flags = HostModifierName.flags(from: withModifiers)
            if rawJSON["function"] as? Bool == true { flags.insert(.function) }
            // A macOS platform entry may also carry the plain modifier booleans
            // the version 1 format used; honour them so a converted function
            // entry keeps its qualifier.
            if rawJSON["shift"] as? Bool == true { flags.insert(.shift) }
            if rawJSON["control"] as? Bool == true { flags.insert(.control) }
            if rawJSON["option"] as? Bool == true { flags.insert(.option) }
            if rawJSON["command"] as? Bool == true { flags.insert(.command) }
            return MacOSKeySpec(keyCode: UInt16(keyCodeInt), requiredModifiers: flags)
        }
        return nil
    }

    /// Build a version 2 entry from a macOS key code, host modifiers and a BBC
    /// target. A key with a neutral `code` and no `function` modifier becomes a
    /// portable `code` entry; otherwise it becomes a `platform: macOS` entry
    /// carrying the native key code (3.4). This is the one place that composes an
    /// entry, so loaded, converted and programmatically built mappings serialise
    /// identically.
    static func make(
        macOSKeyCode: UInt16,
        modifiers: NSEvent.ModifierFlags,
        bbcKeyName: String,
        bbcShift: Bool,
        bbcCtrl: Bool,
        disabled: Bool? = nil,
        action: String? = nil
    ) -> KeyMappingEntry {
        var bbcDict: [String: Any] = ["key": bbcKeyName]
        if bbcShift { bbcDict["shift"] = true }
        if bbcCtrl { bbcDict["ctrl"] = true }
        if let disabled = disabled { bbcDict["disabled"] = disabled }
        if let action = action { bbcDict["action"] = action }

        let usesFunction = modifiers.contains(.function)
        if !usesFunction, let code = W3CKeyCode.code(forMacKeyCode: macOSKeyCode) {
            var dict: [String: Any] = ["code": code, "bbc": bbcDict]
            let names = HostModifierName.names(from: modifiers)
            if !names.isEmpty { dict["with"] = names }
            return KeyMappingEntry(rawJSON: dict)
        }

        // No neutral code (or the function modifier is involved): a macOS
        // platform entry carrying the native key code.
        var dict: [String: Any] = [
            "platform": "macOS",
            "keyCode": Int(macOSKeyCode),
            "bbc": bbcDict
        ]
        let names = HostModifierName.names(from: modifiers)
        if !names.isEmpty { dict["with"] = names }
        if usesFunction { dict["function"] = true }
        return KeyMappingEntry(rawJSON: dict)
    }
}

/// A parsed keyboard mapping file. Reads version 2, converts version 1 on load,
/// and always writes version 2. Unknown top-level fields and entries this client
/// does not interpret are preserved.
struct KeyboardMappingFile {
    /// The current format version (2); a file always serialises as this.
    static let currentVersion = 2

    var id: UUID
    var name: String
    var characterMapping: Bool
    var synchronizeCapsLock: Bool
    var entries: [KeyMappingEntry]

    /// The version the file carried when read. `needsRewrite` is true when it was
    /// older than the current version, so a loader can rewrite it as version 2.
    let originalVersion: Int

    /// Unknown top-level fields, preserved across a rewrite.
    private var extraTopLevel: [String: Any]

    /// Known top-level keys, excluded from the preserved extras.
    private static let knownTopLevelKeys: Set<String> = [
        "version", "id", "name", "characterMapping", "synchronizeCapsLock",
        "keys", "keyMappings"
    ]

    /// Whether the file should be rewritten as the current version.
    var needsRewrite: Bool { originalVersion < Self.currentVersion }

    /// Parse a mapping file. A version 1 file (its entries under `keyMappings`,
    /// keyed on native codes in per-platform sections) is converted to version 2
    /// in memory per docs/keyboard-mapping-files.md section 7.
    init(json: [String: Any]) {
        if let idString = json["id"] as? String,
           let uuid = UUID(uuidString: idString) {
            self.id = uuid
        } else {
            self.id = UUID()
        }

        self.originalVersion = json["version"] as? Int ?? 1
        self.name = json["name"] as? String ?? "Unnamed"
        self.characterMapping = json["characterMapping"] as? Bool ?? true
        self.synchronizeCapsLock = json["synchronizeCapsLock"] as? Bool ?? true

        var extras: [String: Any] = [:]
        for (key, value) in json where !Self.knownTopLevelKeys.contains(key) {
            extras[key] = value
        }
        self.extraTopLevel = extras

        if originalVersion >= 2 {
            let entriesJSON = json["keys"] as? [[String: Any]] ?? []
            self.entries = entriesJSON.map { KeyMappingEntry(rawJSON: $0) }
        } else {
            let entriesJSON = json["keyMappings"] as? [[String: Any]] ?? []
            self.entries = entriesJSON.compactMap { Self.convertVersion1Entry($0) }
        }
    }

    /// Build a file model directly from known fields (serialisation side).
    init(
        id: UUID,
        name: String,
        characterMapping: Bool,
        synchronizeCapsLock: Bool,
        entries: [KeyMappingEntry]
    ) {
        self.id = id
        self.name = name
        self.characterMapping = characterMapping
        self.synchronizeCapsLock = synchronizeCapsLock
        self.entries = entries
        self.originalVersion = Self.currentVersion
        self.extraTopLevel = [:]
    }

    /// Serialise to a version 2 JSON dictionary, preserving every entry verbatim
    /// and any unknown top-level fields.
    func toJSON() -> [String: Any] {
        var json: [String: Any] = [
            "version": Self.currentVersion,
            "id": id.uuidString,
            "name": name,
            "characterMapping": characterMapping,
            "synchronizeCapsLock": synchronizeCapsLock,
            "keys": entries.map { $0.rawJSON }
        ]
        for (key, value) in extraTopLevel where json[key] == nil {
            json[key] = value
        }
        return json
    }

    // MARK: - Version 1 conversion

    /// Convert one version 1 entry to a version 2 entry per section 7:
    /// - `macOS.keyCode` through the key-code table becomes `code`;
    ///   `shift`/`control`/`option`/`command` become `with`.
    /// - `function: true` has no neutral modifier, so the entry stays a
    ///   `platform: macOS` entry carrying the original key code; a key the table
    ///   cannot name stays a `platform: macOS` entry too.
    /// - `windows.VKCode` is a virtual key, not a position: dropped when the
    ///   macOS key gives a code, kept as a `platform: windows` entry otherwise.
    /// - A section for another platform with no macOS section is preserved as a
    ///   `platform` entry.
    /// - `bbc.keyName` becomes `bbc.key`; `shift`/`ctrl`/`disabled`/`action`
    ///   carry over, and the legacy "ShiftLock" spelling becomes the catalogue's
    ///   "Shift Lock".
    static func convertVersion1Entry(_ entry: [String: Any]) -> KeyMappingEntry? {
        let bbcDict = (entry["bbc"] as? [String: Any]).map(convertVersion1BBC)

        if let macOS = entry["macOS"] as? [String: Any],
           let keyCodeInt = macOS["keyCode"] as? Int {
            let keyCode = UInt16(keyCodeInt)
            let function = macOS["function"] as? Bool ?? false
            var modifiers: [String] = []
            if macOS["shift"] as? Bool == true { modifiers.append("Shift") }
            if macOS["control"] as? Bool == true { modifiers.append("Control") }
            if macOS["option"] as? Bool == true { modifiers.append("Alt") }
            if macOS["command"] as? Bool == true { modifiers.append("Meta") }

            if !function, let code = W3CKeyCode.code(forMacKeyCode: keyCode) {
                var dict: [String: Any] = ["code": code]
                if !modifiers.isEmpty { dict["with"] = modifiers }
                if let bbcDict = bbcDict { dict["bbc"] = bbcDict }
                return KeyMappingEntry(rawJSON: dict)
            }

            // function, or a key the table cannot name: a macOS platform entry.
            var dict: [String: Any] = ["platform": "macOS", "keyCode": keyCodeInt]
            if !modifiers.isEmpty { dict["with"] = modifiers }
            if function { dict["function"] = true }
            if let bbcDict = bbcDict { dict["bbc"] = bbcDict }
            return KeyMappingEntry(rawJSON: dict)
        }

        // No macOS section: preserve a foreign-platform entry so it round-trips.
        if let windows = entry["windows"] as? [String: Any],
           let vk = windows["VKCode"] as? Int {
            var dict: [String: Any] = ["platform": "windows", "VKCode": vk]
            if let alt = windows["alt"] as? Bool, alt { dict["alt"] = true }
            if let bbcDict = bbcDict { dict["bbc"] = bbcDict }
            return KeyMappingEntry(rawJSON: dict)
        }

        return nil
    }

    /// Convert a version 1 `bbc` section to version 2: `keyName` -> `key`, with
    /// the legacy "ShiftLock" spelling normalised to the catalogue's "Shift Lock".
    private static func convertVersion1BBC(_ bbc: [String: Any]) -> [String: Any] {
        var dict: [String: Any] = [:]
        if let keyName = bbc["keyName"] as? String {
            dict["key"] = keyName == "ShiftLock" ? "Shift Lock" : keyName
        }
        if let shift = bbc["shift"] as? Bool, shift { dict["shift"] = true }
        if let ctrl = bbc["ctrl"] as? Bool, ctrl { dict["ctrl"] = true }
        if let disabled = bbc["disabled"] as? Bool { dict["disabled"] = disabled }
        if let action = bbc["action"] as? String { dict["action"] = action }
        return dict
    }
}
