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

/// Manages keyboard mappings: built-in and user-defined.
///
/// Responsible for:
/// - Creating built-in mappings
/// - Loading/saving user mappings from disk
/// - Tracking the active mapping
/// - Import/export of mapping files
@MainActor
final class KeyboardMappingManager: ObservableObject {

    /// Well-known UUID for the default logical mapping
    static let defaultLogicalMappingID = UUID(uuidString: "00000000-0000-0000-0000-000000000001")!

    /// All available mappings (built-in + user-defined)
    @Published private(set) var mappings: [KeyboardMapping] = []

    /// The previously active mapping (for toggle feature)
    @Published private(set) var previousMapping: KeyboardMapping?

    /// The currently active mapping
    @Published var activeMapping: KeyboardMapping? {
        didSet {
            // Track previous mapping for toggle feature
            if let old = oldValue, old.id != activeMapping?.id {
                previousMapping = old
            }
            // Reset session overrides when mapping changes
            capsLockSyncOverride = nil
            disabledKeysOverride = nil
        }
    }

    /// Session override for Caps Lock sync (nil = use mapping default)
    @Published var capsLockSyncOverride: Bool? = nil

    /// Session overrides for disabled keys (nil = use mapping defaults)
    /// Maps BBC key name to disabled state
    @Published var disabledKeysOverride: [String: Bool]? = nil

    /// Effective Caps Lock sync setting (respects session override)
    var isCapsLockSyncEnabled: Bool {
        if let override = capsLockSyncOverride {
            return override
        }
        return activeMapping?.synchronizeCapsLock ?? true
    }

    /// Check if a BBC key is currently disabled (respects session override)
    func isKeyDisabled(_ keyName: String) -> Bool {
        // Session override takes priority
        if let override = disabledKeysOverride?[keyName] {
            return override
        }
        // Fall back to mapping default
        return activeMapping?.disableableKeys[keyName] ?? false
    }

    /// Get sorted list of disableable keys in the active mapping
    var disableableKeyNames: [String] {
        return (activeMapping?.disableableKeys.keys.sorted()) ?? []
    }

    /// Host key label for the Break key in the active mapping (e.g. "⌥F12"), or nil if unmapped
    var breakKeyLabel: String? {
        guard let mapping = activeMapping else { return nil }
        for entry in mapping.allEntries {
            guard let macOSSpec = entry.macOS,
                  let bbcSpec = entry.bbc,
                  bbcSpec.keyName == "Break" else { continue }
            var components: [String] = []
            if macOSSpec.requiredModifiers.contains(.control) { components.append("⌃") }
            if macOSSpec.requiredModifiers.contains(.option) { components.append("⌥") }
            if macOSSpec.requiredModifiers.contains(.shift) { components.append("⇧") }
            if macOSSpec.requiredModifiers.contains(.command) { components.append("⌘") }
            let keyName = MacKeyCode.name(for: macOSSpec.keyCode)
            components.append(keyName)
            return components.joined()
        }
        return nil
    }

    /// Represents a key mapping with action for display in reference table
    struct MappingReferenceEntry: Identifiable {
        let id = UUID()
        let keyCode: UInt16
        let modifiers: NSEvent.ModifierFlags
        let action: String

        /// Format host key with macOS modifier symbols
        var hostKeyLabel: String {
            var components: [String] = []

            if modifiers.contains(.control) { components.append("⌃") }
            if modifiers.contains(.option) { components.append("⌥") }
            if modifiers.contains(.shift) { components.append("⇧") }
            if modifiers.contains(.command) { components.append("⌘") }

            // Get base key name from keyCode
            let keyName = MacKeyCode.name(for: keyCode)
            components.append(keyName)

            return components.joined()
        }
    }

    /// Get ordered mapping reference entries (only those with actions)
    var mappingReferenceEntries: [MappingReferenceEntry] {
        guard let mapping = activeMapping else { return [] }

        var entries: [MappingReferenceEntry] = []

        for entry in mapping.allEntries {
            // Only include entries with both macOS and BBC sections
            guard let macOSSpec = entry.macOS,
                  let bbcSpec = entry.bbc,
                  let action = bbcSpec.action else {
                continue
            }

            entries.append(MappingReferenceEntry(
                keyCode: macOSSpec.keyCode,
                modifiers: macOSSpec.requiredModifiers,
                action: action
            ))
        }

        return entries
    }

    /// The BBC key cache (populated from core)
    let bbcKeyCache = BBCKeyCache()

    /// Directory for user mapping files
    private let userMappingsDirectory: URL

    init() {
        // Set up user mappings directory
        let appSupport = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first!
        userMappingsDirectory = appSupport.appendingPathComponent("Beebium/KeyboardMappings", isDirectory: true)

        // Create built-in mappings
        let builtIns = Self.createBuiltInMappings()
        mappings = builtIns

        // Set default active mapping
        activeMapping = builtIns.first
    }

    // MARK: - Cache Loading

    /// Load the BBC key cache from a gRPC response
    func loadCache(from response: Beebium_AllKeyMappingsResponse) {
        bbcKeyCache.load(from: response)
    }

    /// Whether the cache is ready for use
    var isCacheLoaded: Bool {
        bbcKeyCache.isLoaded
    }

    // MARK: - User Mappings

    /// Load user-defined mappings from disk
    func loadUserMappings() throws {
        // Ensure directory exists
        try FileManager.default.createDirectory(at: userMappingsDirectory, withIntermediateDirectories: true)

        // Find all JSON files
        let fileURLs = try FileManager.default.contentsOfDirectory(
            at: userMappingsDirectory,
            includingPropertiesForKeys: nil,
            options: .skipsHiddenFiles
        ).filter { $0.pathExtension == "json" }

        // Load each file
        for fileURL in fileURLs {
            do {
                let data = try Data(contentsOf: fileURL)
                if let json = try JSONSerialization.jsonObject(with: data) as? [String: Any] {
                    let mapping = KeyboardMapping(json: json)
                    mappings.append(mapping)
                    print("[KeyboardMappingManager] Loaded mapping: \(mapping.name)")
                }
            } catch {
                print("[KeyboardMappingManager] Failed to load \(fileURL.lastPathComponent): \(error)")
            }
        }
    }

    /// Save all user-defined mappings to disk
    func saveUserMappings() throws {
        // Ensure directory exists
        try FileManager.default.createDirectory(at: userMappingsDirectory, withIntermediateDirectories: true)

        for mapping in mappings where !mapping.isBuiltIn {
            let json = mapping.toJSON()
            let data = try JSONSerialization.data(withJSONObject: json, options: [.prettyPrinted, .sortedKeys])
            let fileURL = userMappingsDirectory.appendingPathComponent("\(mapping.id.uuidString).json")
            try data.write(to: fileURL)
            print("[KeyboardMappingManager] Saved mapping: \(mapping.name)")
        }
    }

    /// Save a specific mapping to disk
    func saveMapping(_ mapping: KeyboardMapping) throws {
        guard !mapping.isBuiltIn else { return }

        try FileManager.default.createDirectory(at: userMappingsDirectory, withIntermediateDirectories: true)

        let json = mapping.toJSON()
        let data = try JSONSerialization.data(withJSONObject: json, options: [.prettyPrinted, .sortedKeys])
        let fileURL = userMappingsDirectory.appendingPathComponent("\(mapping.id.uuidString).json")
        try data.write(to: fileURL)
    }

    // MARK: - Mapping Management

    /// The mapping that Cmd-K would switch to (nil if no valid target)
    var toggleTargetMapping: KeyboardMapping? {
        guard let current = activeMapping else { return nil }

        if current.id == Self.defaultLogicalMappingID {
            return previousMapping
        } else {
            return mappings.first(where: { $0.id == Self.defaultLogicalMappingID })
        }
    }

    /// Toggle between current mapping and default logical (or previous mapping)
    func toggleToDefaultLogical() {
        guard let current = activeMapping else { return }

        if current.id == Self.defaultLogicalMappingID {
            // Currently on default logical, switch back to previous
            if let previous = previousMapping {
                activeMapping = previous
            }
        } else {
            // Switch to default logical
            if let defaultLogical = mappings.first(where: { $0.id == Self.defaultLogicalMappingID }) {
                activeMapping = defaultLogical
            }
        }
    }

    /// Clone an existing mapping with a new name
    func cloneMapping(_ mapping: KeyboardMapping, newName: String) -> KeyboardMapping {
        let uniqueName = makeUniqueName(newName)
        let clone = mapping.clone(newName: uniqueName)
        mappings.append(clone)
        return clone
    }

    /// Delete a user-defined mapping
    func deleteMapping(_ mapping: KeyboardMapping) {
        guard !mapping.isBuiltIn else { return }

        mappings.removeAll { $0.id == mapping.id }

        // Delete file if it exists
        let fileURL = userMappingsDirectory.appendingPathComponent("\(mapping.id.uuidString).json")
        try? FileManager.default.removeItem(at: fileURL)

        // If we deleted the active mapping, switch to first available
        if activeMapping?.id == mapping.id {
            activeMapping = mappings.first
        }
    }

    /// Get the file URL for a user-defined mapping (for Reveal in Finder)
    /// Returns nil for built-in mappings
    func fileURL(for mapping: KeyboardMapping) -> URL? {
        guard !mapping.isBuiltIn else { return nil }
        return userMappingsDirectory.appendingPathComponent("\(mapping.id.uuidString).json")
    }

    /// Import a mapping from JSON data
    func importMapping(from data: Data) throws -> KeyboardMapping {
        guard let json = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            throw MappingError.invalidJSON
        }

        let mapping = KeyboardMapping(json: json)

        // Ensure unique name
        mapping.name = makeUniqueName(mapping.name)

        mappings.append(mapping)

        // Save to disk
        try saveMapping(mapping)

        return mapping
    }

    /// Export a mapping to JSON data
    func exportMapping(_ mapping: KeyboardMapping) throws -> Data {
        let json = mapping.toJSON()
        return try JSONSerialization.data(withJSONObject: json, options: [.prettyPrinted, .sortedKeys])
    }

    /// Import a mapping from a file URL
    func importMapping(from url: URL) throws -> KeyboardMapping {
        let data = try Data(contentsOf: url)
        return try importMapping(from: data)
    }

    /// Export a mapping to a file URL
    func exportMapping(_ mapping: KeyboardMapping, to url: URL) throws {
        let data = try exportMapping(mapping)
        try data.write(to: url)
    }

    // MARK: - Private

    private func makeUniqueName(_ base: String) -> String {
        var name = base
        var counter = 2
        while mappings.contains(where: { $0.name == name }) {
            name = "\(base) \(counter)"
            counter += 1
        }
        return name
    }

    private static func createBuiltInMappings() -> [KeyboardMapping] {
        guard let mappingsDirURL = Bundle.main.url(forResource: "KeyboardMappings", withExtension: nil) else {
            fatalError("KeyboardMappings directory not found in bundle")
        }

        do {
            let fileURLs = try FileManager.default.contentsOfDirectory(
                at: mappingsDirURL,
                includingPropertiesForKeys: nil,
                options: .skipsHiddenFiles
            ).filter { $0.pathExtension == "json" }

            var mappings: [KeyboardMapping] = []
            for fileURL in fileURLs {
                let data = try Data(contentsOf: fileURL)
                guard let json = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
                    print("[KeyboardMappingManager] Skipping invalid JSON: \(fileURL.lastPathComponent)")
                    continue
                }
                let mapping = KeyboardMapping(json: json, isBuiltIn: true)
                mappings.append(mapping)
            }

            return mappings.sorted { lhs, rhs in
                if lhs.id == defaultLogicalMappingID { return true }
                if rhs.id == defaultLogicalMappingID { return false }
                return lhs.name < rhs.name
            }
        } catch {
            fatalError("Failed to load built-in keyboard mappings: \(error)")
        }
    }

    // MARK: - Errors

    enum MappingError: Error {
        case invalidJSON
        case fileNotFound
    }
}
