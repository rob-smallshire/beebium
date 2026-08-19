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
import UniformTypeIdentifiers

/// Error type for core launch failures
enum CoreLaunchError: LocalizedError {
    case launchFailed(String)
    case timeout
    case processExited(String)

    var errorDescription: String? {
        switch self {
        case .launchFailed(let message):
            return message
        case .timeout:
            return "Timeout waiting for server to start"
        case .processExited(let message):
            return message
        }
    }
}

/// Manages machine presets by discovering preset files and querying their configurations.
///
/// Discovers presets via:
/// 1. `BEEBIUM_SERVERS_DIRPATH` environment variable (if set)
/// 2. Fallback to `~/Code/beebium/build/src/server/` for development
///
/// Preset files are located in the `presets/` subdirectory with `.preset.beebium` extension.
/// Each preset declares a `model` field that maps to an executable (beebium-{model}).
@MainActor
class PresetManager: ObservableObject {
    static let shared = PresetManager()

    @Published private(set) var systemPresets: [MachinePreset] = []
    @Published private(set) var userPresets: [MachinePreset] = []
    @Published private(set) var isDiscovering = false
    @Published private(set) var discoveryError: String?

    /// Cached user presets directory path (retrieved from CLI)
    private var cachedUserPresetsDirpath: String?

    /// Numbers machines per preset, for this run of the app only.
    private let nameSequence = MachineNameSequence()

    /// Disc image picker content types, fetched from the server once and kept
    /// for the session (disc format support does not change while the app
    /// runs).
    private var cachedDiscImageContentTypes: [UTType]?

    private init() {}

    /// Discover preset files and build system and user presets.
    func discoverPresets() async {
        isDiscovering = true
        discoveryError = nil
        systemPresets = []
        userPresets = []

        let dirpath = serversDirpath()
        let systemPresetsDirpathValue = systemPresetsDirpath()
        var discoveredSystem: [MachinePreset] = []
        var discoveredUser: [MachinePreset] = []
        var configFailures: [String] = []

        NSLog("[PresetManager] Searching for system presets in: \(systemPresetsDirpathValue)")

        // Discover system presets
        let (systemFilepaths, directoryError) = findPresetFiles(in: systemPresetsDirpathValue)
        NSLog("[PresetManager] Found \(systemFilepaths.count) system preset file(s)")

        for presetFilepath in systemFilepaths {
            if let preset = await loadPreset(from: presetFilepath, source: .systemPreset, serversDirpath: dirpath, configFailures: &configFailures) {
                discoveredSystem.append(preset)
            }
        }

        // Discover user presets (need an executable to query the user presets directory)
        if let firstSystemPreset = discoveredSystem.first,
           let userDirpath = await fetchUserPresetsDirpath(using: firstSystemPreset.coreExecutablePath) {
            NSLog("[PresetManager] Searching for user presets in: \(userDirpath)")
            let (userFilepaths, _) = findPresetFiles(in: userDirpath)
            NSLog("[PresetManager] Found \(userFilepaths.count) user preset file(s)")

            for presetFilepath in userFilepaths {
                if let preset = await loadPreset(from: presetFilepath, source: .userPreset, serversDirpath: dirpath, configFailures: &configFailures) {
                    discoveredUser.append(preset)
                }
            }
        }

        systemPresets = sortPresets(discoveredSystem)
        userPresets = sortPresets(discoveredUser)
        isDiscovering = false

        if let directoryError = directoryError {
            discoveryError = directoryError
        } else if systemFilepaths.isEmpty {
            discoveryError = "No preset files found in \(systemPresetsDirpathValue)"
        } else if discoveredSystem.isEmpty {
            let failedList = configFailures.joined(separator: ", ")
            discoveryError = "Found \(systemFilepaths.count) preset(s) but failed to load: \(failedList)"
        }
    }

    /// Load a single preset from a file path.
    private func loadPreset(from presetFilepath: String, source: MachinePreset.Source, serversDirpath: String, configFailures: inout [String]) async -> MachinePreset? {
        let filename = URL(fileURLWithPath: presetFilepath).lastPathComponent
        let presetId = presetIdFromFilename(filename)
        NSLog("[PresetManager] Processing preset: \(filename) (id: \(presetId))")

        guard let presetData = parsePresetFile(at: presetFilepath) else {
            configFailures.append(filename)
            NSLog("[PresetManager] Failed to parse preset file: \(filename)")
            return nil
        }

        let executablePath = "\(serversDirpath)/beebium-\(presetData.model)"
        guard FileManager.default.isExecutableFile(atPath: executablePath) else {
            NSLog("[PresetManager] Executable not found for model '\(presetData.model)': \(executablePath)")
            return nil
        }

        let (schema, error) = await fetchPresetSchema(from: executablePath)
        guard let schema = schema else {
            configFailures.append(filename)
            NSLog("[PresetManager] Failed to get schema from \(presetData.model): \(error ?? "unknown error")")
            return nil
        }

        let preset = MachinePreset(
            id: UUID(),
            presetId: presetId,
            name: presetData.name ?? schema.model.name,
            coreExecutablePath: executablePath,
            presetFilepath: presetFilepath,
            source: source,
            modelName: schema.model.name,
            modelDescription: presetData.description ?? schema.model.description,
            releaseDate: presetData.releaseDate,
            configuration: [:]
        )
        NSLog("[PresetManager] Discovered: \(preset.name)")
        return preset
    }

    /// Extract preset ID from filename (e.g., "bbc-model-b.preset.beebium" -> "bbc-model-b")
    private func presetIdFromFilename(_ filename: String) -> String {
        let suffix = ".preset.beebium"
        if filename.hasSuffix(suffix) {
            return String(filename.dropLast(suffix.count))
        }
        return filename
    }

    /// Get the directory path where server executables are located.
    ///
    /// Search order:
    /// 1. `BEEBIUM_SERVERS_DIRPATH` environment variable (for development/testing)
    /// 2. App bundle `Resources/servers` directory (for distribution)
    /// 3. Development fallback path
    private func serversDirpath() -> String {
        // 1. Environment variable override (for development/testing)
        if let envPath = ProcessInfo.processInfo.environment["BEEBIUM_SERVERS_DIRPATH"] {
            return envPath
        }

        // 2. App bundle (for distribution). The embedded servers, their
        // plugin tree, and their bundled native dependencies live under
        // Resources/servers -- not Frameworks, which codesign validates as
        // code-only and would reject the plugins' manifest.json files. See
        // the "Embed Server Executables" build phase in project.yml.
        if let resourcePath = Bundle.main.resourcePath {
            let serversPath = "\(resourcePath)/servers"
            if FileManager.default.fileExists(atPath: "\(serversPath)/beebium-model-b") {
                return serversPath
            }
        }

        // 3. Development fallback
        let homeDir = FileManager.default.homeDirectoryForCurrentUser
        return homeDir.appendingPathComponent("Code/beebium/build/src/server").path
    }

    /// Get the bundled ROM directory path, if ROMs are bundled in the app.
    /// Returns nil if no bundled ROMs are found.
    private func bundledRomDirpath() -> String? {
        guard let resourcePath = Bundle.main.resourcePath else { return nil }
        let romPath = "\(resourcePath)/roms"
        return FileManager.default.fileExists(atPath: romPath) ? romPath : nil
    }

    /// Get the bundled presets directory path, if presets are bundled in the app.
    /// Returns nil if no bundled presets are found.
    ///
    /// Presets live alongside the embedded servers
    /// (Resources/servers/presets/) so the server binary's own
    /// PresetPaths::get_system_presets_dirpath() can resolve them by
    /// ID for --from. The Swift side reads from the same directory.
    private func bundledPresetsDirpath() -> String? {
        guard let resourcePath = Bundle.main.resourcePath else { return nil }
        let presetsPath = "\(resourcePath)/servers/presets"
        return FileManager.default.fileExists(atPath: presetsPath) ? presetsPath : nil
    }

    /// Find preset files in a directory (files with `.preset.beebium` extension).
    /// Returns (filepaths, errorMessage) - errorMessage is nil on success.
    private func findPresetFiles(in dirpath: String) -> ([String], String?) {
        let fm = FileManager.default

        var isDirectory: ObjCBool = false
        guard fm.fileExists(atPath: dirpath, isDirectory: &isDirectory), isDirectory.boolValue else {
            NSLog("[PresetManager] Presets directory does not exist: \(dirpath)")
            return ([], "Presets directory does not exist: \(dirpath)")
        }

        guard let contents = try? fm.contentsOfDirectory(atPath: dirpath) else {
            NSLog("[PresetManager] Cannot read presets directory: \(dirpath)")
            return ([], "Cannot read presets directory: \(dirpath)")
        }

        let presetFilepaths = contents
            .filter { $0.hasSuffix(".preset.beebium") }
            .map { "\(dirpath)/\($0)" }
            .sorted()

        return (presetFilepaths, nil)
    }

    /// Parse a preset file and extract its data.
    private func parsePresetFile(at filepath: String) -> PresetFileData? {
        guard let data = FileManager.default.contents(atPath: filepath) else {
            return nil
        }

        do {
            return try JSONDecoder().decode(PresetFileData.self, from: data)
        } catch {
            NSLog("[PresetManager] JSON decode error for \(filepath): \(error)")
            return nil
        }
    }

    /// Invoke `describe-preset-schema` on a server executable and parse the JSON response.
    /// Returns (schema, errorMessage) - errorMessage is nil on success.
    private func fetchPresetSchema(from executablePath: String) async -> (PresetSchema?, String?) {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: executablePath)
        process.arguments = ["describe-preset-schema"]

        let stdoutPipe = Pipe()
        let stderrPipe = Pipe()
        process.standardOutput = stdoutPipe
        process.standardError = stderrPipe

        do {
            try process.run()
            process.waitUntilExit()

            guard process.terminationStatus == 0 else {
                let stderrData = stderrPipe.fileHandleForReading.readDataToEndOfFile()
                let stderrText = String(data: stderrData, encoding: .utf8)?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
                let errorMsg = stderrText.isEmpty
                    ? "exit status \(process.terminationStatus)"
                    : stderrText
                NSLog("[PresetManager] Process exited with status \(process.terminationStatus): \(errorMsg)")
                return (nil, errorMsg)
            }

            let data = stdoutPipe.fileHandleForReading.readDataToEndOfFile()
            let schema = try JSONDecoder().decode(PresetSchema.self, from: data)
            return (schema, nil)
        } catch {
            NSLog("[PresetManager] Failed to query \(executablePath): \(error)")
            return (nil, error.localizedDescription)
        }
    }

    /// Fetch and decode the storage schema section for a preset's model.
    ///
    /// This queries the preset's core executable for its schema and extracts
    /// the storage section, which includes FDC options and floppy drive configuration.
    func fetchStorageSchema(for preset: MachinePreset) async -> StorageSchemaSection? {
        // fetchStorageSectionFromSchema runs `describe-preset-schema` itself
        // and returns nil for every reason the fetch can fail, so probing with
        // fetchPresetSchema first only ran the core executable a second time
        // and discarded the answer. It logs its own failures.
        guard let section = await fetchStorageSectionFromSchema(
            executablePath: preset.coreExecutablePath
        ) else {
            NSLog("[PresetManager] Failed to fetch storage schema for \(preset.presetId)")
            return nil
        }
        return section
    }

    /// Fetch the storage section with full detail from the schema JSON.
    private func fetchStorageSectionFromSchema(executablePath: String) async -> StorageSchemaSection? {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: executablePath)
        process.arguments = ["describe-preset-schema"]

        let stdoutPipe = Pipe()
        process.standardOutput = stdoutPipe
        process.standardError = FileHandle.nullDevice

        do {
            try process.run()
            process.waitUntilExit()

            guard process.terminationStatus == 0 else { return nil }

            let data = stdoutPipe.fileHandleForReading.readDataToEndOfFile()

            // Decode as a dictionary to extract the sections array
            guard let json = try JSONSerialization.jsonObject(with: data) as? [String: Any],
                  let sections = json["sections"] as? [[String: Any]] else {
                return nil
            }

            // Find the storage section
            guard let storageSection = sections.first(where: { ($0["type"] as? String) == "storage" }) else {
                return nil
            }

            // Re-encode and decode as StorageSchemaSection
            let sectionData = try JSONSerialization.data(withJSONObject: storageSection)
            return try JSONDecoder().decode(StorageSchemaSection.self, from: sectionData)
        } catch {
            NSLog("[PresetManager] Failed to decode storage schema: \(error)")
            return nil
        }
    }

    /// Fetch the sideways_bank schema section for a preset's model.
    ///
    /// Describes the machine's physical sideways sockets (and aliasing) plus the
    /// ROMs it installs by default, used to build the Memory tab.
    func fetchSidewaysSchema(for preset: MachinePreset) async -> SidewaysSchemaSection? {
        return await fetchSidewaysSectionFromSchema(executablePath: preset.coreExecutablePath)
    }

    /// Read the sideways slot assignments a preset file declares (empty if none).
    /// Lets the Memory tab show what the preset already configures.
    func sidewaysSlots(for preset: MachinePreset) -> [PresetSidewaysSlot] {
        guard let data = FileManager.default.contents(atPath: preset.presetFilepath),
              let parsed = try? JSONDecoder().decode(PresetFileData.self, from: data) else {
            return []
        }
        return parsed.sidewaysBank?.slots ?? []
    }

    /// Parse a ROM image's header via the `describe-rom` subcommand, so the UI
    /// can show its real title/version. `image` is a ROM library name or path;
    /// returns nil if it can't be read or parsed.
    func fetchRomHeader(image: String, executablePath: String) async -> RomHeaderInfo? {
        var arguments = ["describe-rom", image]
        // Resolve bare ROM names against the bundled ROM directory when present;
        // otherwise the server falls back to its own search path.
        if let romDir = bundledRomDirpath() {
            arguments.append(contentsOf: ["--rom-dir", romDir])
        }

        let (output, error) = await runCli(executable: executablePath, arguments: arguments)
        if error != nil { return nil }
        guard let data = output.data(using: .utf8),
              let info = try? JSONDecoder().decode(RomHeaderInfo.self, from: data) else {
            return nil
        }
        return info
    }

    /// A core executable to ask about disc formats when no particular machine
    /// is in hand.
    ///
    /// Disc format support lives in the shared core, identical across every
    /// server variant, so any of them answers the same -- validity and the
    /// accepted extensions do not depend on which machine you picked. Nil only
    /// before presets have been discovered.
    var anyCoreExecutablePath: String? {
        systemPresets.first?.coreExecutablePath
    }

    /// The disc image formats the server can load, as content types for a
    /// file picker's filter.
    ///
    /// Sourced from the server via list-floppy-formats and cached for the
    /// session, so the client keeps no list of its own -- the picker offers
    /// exactly the formats the machine will load, and the two cannot drift.
    /// Empty (an unfiltered picker) if no executable can be asked or its
    /// output cannot be parsed, which is a better failure than a stale
    /// hardcoded list.
    func discImageContentTypes() async -> [UTType] {
        if let cached = cachedDiscImageContentTypes {
            return cached
        }
        guard let executablePath = anyCoreExecutablePath else {
            return []
        }
        let (output, error) = await runCli(
            executable: executablePath,
            arguments: ["--format", "jsonl", "list-floppy-formats"])
        if error != nil {
            return []
        }
        let types = Self.parseDiscImageExtensions(fromFormatList: output)
            .compactMap { UTType(filenameExtension: $0) }
        cachedDiscImageContentTypes = types
        return types
    }

    /// The extensions from list-floppy-formats' JSONL output, without the
    /// leading dot and lowercased. A pure, side-effect-free seam, so the
    /// parse can be tested without running a server.
    nonisolated static func parseDiscImageExtensions(fromFormatList jsonl: String) -> [String] {
        struct Line: Decodable { let extensions: [String] }
        var out: [String] = []
        for raw in jsonl.split(whereSeparator: \.isNewline) {
            guard let data = raw.data(using: .utf8),
                  let line = try? JSONDecoder().decode(Line.self, from: data) else {
                continue
            }
            for ext in line.extensions {
                let bare = ext.hasPrefix(".") ? String(ext.dropFirst()) : ext
                if !bare.isEmpty {
                    out.append(bare.lowercased())
                }
            }
        }
        return out
    }

    /// Ask the server executable whether `path` is a disc image, and what.
    ///
    /// The same rule the machine applies on insert, reached before it is
    /// launched -- so the preset editor accepts exactly what the running
    /// machine would. Nil if the executable could not be run at all (as
    /// opposed to running and reporting the file is not a disc image, which
    /// comes back as a DiscImageInfo with recognised == false).
    func describeDiscImage(path: String, executablePath: String) async -> DiscImageInfo? {
        let (output, error) = await runCli(
            executable: executablePath,
            arguments: ["describe-disc-image", path])
        // A non-zero exit means the file could not be read at all; the
        // "not a disc image" verdict comes back as exit 0 with recognised
        // false, so it is not an error here.
        if error != nil { return nil }
        guard let data = output.data(using: .utf8),
              let info = try? JSONDecoder().decode(DiscImageInfo.self, from: data) else {
            return nil
        }
        return info
    }

    /// Fetch the sideways_bank section with full detail from the schema JSON.
    private func fetchSidewaysSectionFromSchema(executablePath: String) async -> SidewaysSchemaSection? {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: executablePath)
        process.arguments = ["describe-preset-schema"]

        let stdoutPipe = Pipe()
        process.standardOutput = stdoutPipe
        process.standardError = FileHandle.nullDevice

        do {
            try process.run()
            process.waitUntilExit()

            guard process.terminationStatus == 0 else { return nil }

            let data = stdoutPipe.fileHandleForReading.readDataToEndOfFile()

            guard let json = try JSONSerialization.jsonObject(with: data) as? [String: Any],
                  let sections = json["sections"] as? [[String: Any]] else {
                return nil
            }

            guard let sidewaysSection = sections.first(where: { ($0["type"] as? String) == "sideways_bank" }) else {
                return nil
            }

            let sectionData = try JSONSerialization.data(withJSONObject: sidewaysSection)
            return try JSONDecoder().decode(SidewaysSchemaSection.self, from: sectionData)
        } catch {
            NSLog("[PresetManager] Failed to decode sideways schema: \(error)")
            return nil
        }
    }

    /// Sort presets by release date (chronological), then by name (natural alphanumeric).
    private func sortPresets(_ presets: [MachinePreset]) -> [MachinePreset] {
        presets.sorted { lhs, rhs in
            let lhsDate = normalizedReleaseDate(lhs.releaseDate)
            let rhsDate = normalizedReleaseDate(rhs.releaseDate)

            if lhsDate != rhsDate {
                return lhsDate < rhsDate
            }

            return lhs.name.localizedStandardCompare(rhs.name) == .orderedAscending
        }
    }

    /// Normalize a release date string for sorting.
    /// Converts YYYY, YYYY-MM, YYYY-MM-DD to YYYY-MM-DD with missing parts as 00.
    private func normalizedReleaseDate(_ date: String?) -> String {
        guard let date = date else { return "9999-99-99" }

        let parts = date.split(separator: "-")
        let year = parts.count > 0 ? String(parts[0]) : "9999"
        let month = parts.count > 1 ? String(parts[1]) : "00"
        let day = parts.count > 2 ? String(parts[2]) : "00"

        return "\(year)-\(month)-\(day)"
    }

    // MARK: - User Presets Directory

    /// Get the user presets directory path by querying the CLI.
    func userPresetsDirpath() async -> String? {
        if let cached = cachedUserPresetsDirpath {
            return cached
        }

        // Need at least one system preset to determine executable
        guard let firstPreset = systemPresets.first else {
            NSLog("[PresetManager] No system presets available to query user presets directory")
            return nil
        }

        return await fetchUserPresetsDirpath(using: firstPreset.coreExecutablePath)
    }

    /// Fetch the user presets directory path using a specific executable.
    private func fetchUserPresetsDirpath(using executablePath: String) async -> String? {
        if let cached = cachedUserPresetsDirpath {
            return cached
        }

        let (output, error) = await runCli(executable: executablePath, arguments: ["report-presets-dirpath"])
        if let error = error {
            NSLog("[PresetManager] Failed to get user presets directory: \(error)")
            return nil
        }

        let dirpath = output.trimmingCharacters(in: .whitespacesAndNewlines)
        cachedUserPresetsDirpath = dirpath
        return dirpath
    }

    /// Get the system presets directory path.
    ///
    /// Checks bundled presets first, then falls back to server directory.
    func systemPresetsDirpath() -> String {
        if let bundledPath = bundledPresetsDirpath() {
            return bundledPath
        }
        return "\(serversDirpath())/presets"
    }

    // MARK: - Preset Management

    /// Duplicate a preset with a new name.
    /// - Parameters:
    ///   - preset: The source preset to duplicate
    ///   - newName: The name for the new preset
    /// - Returns: The ID of the created preset, or nil on failure
    func duplicatePreset(_ preset: MachinePreset, newName: String) async -> String? {
        let arguments = ["create-preset", "--name", newName, "--from", preset.presetId]
        let (output, error) = await runCli(executable: preset.coreExecutablePath, arguments: arguments)
        if let error = error {
            NSLog("[PresetManager] Failed to duplicate preset: \(error)")
            return nil
        }

        let newPresetId = output.trimmingCharacters(in: .whitespacesAndNewlines)
        NSLog("[PresetManager] Created preset: \(newPresetId)")

        // Reload presets to pick up the new one
        await discoverPresets()

        return newPresetId
    }

    /// Create a new user preset by snapshotting an existing one and
    /// layering the user's CLI overrides on top.
    ///
    /// The user's sideways/FDC/etc. choices are baked into the new
    /// preset so that selecting it later launches the same machine
    /// without having to repeat the configuration. Storage state
    /// (mounted disc images) is intentionally NOT folded in - those
    /// are runtime concerns, not preset configuration.
    ///
    /// - Parameters:
    ///   - basedOn: The preset to use as a starting point.
    ///   - newName: Display name for the new preset.
    ///   - sidewaysArguments: `--sideways SLOT:TYPE[:IMAGE]` pairs from
    ///     the configurator. Each pair is two strings: the flag and the value.
    /// - Returns: `(presetId, errorMessage)` - exactly one is non-nil.
    ///   On success errorMessage is nil and presetId is the new preset's
    ///   slug. On failure presetId is nil and errorMessage carries the
    ///   actual stderr from the create-preset subcommand (name conflict,
    ///   validation error, etc.) so the UI can surface it instead of
    ///   guessing.
    func createPreset(basedOn preset: MachinePreset,
                      newName: String,
                      sidewaysArguments: [String]) async -> (String?, String?) {
        var arguments: [String] = [
            "create-preset",
            "--name", newName,
            "--from", preset.presetId,
        ]
        arguments.append(contentsOf: sidewaysArguments)

        let (output, error) = await runCli(
            executable: preset.coreExecutablePath,
            arguments: arguments
        )
        if let error = error {
            NSLog("[PresetManager] Failed to create preset: \(error)")
            return (nil, error.trimmingCharacters(in: .whitespacesAndNewlines))
        }

        let newPresetId = output.trimmingCharacters(in: .whitespacesAndNewlines)
        NSLog("[PresetManager] Created preset: \(newPresetId) from \(preset.presetId)")

        // Reload presets so the new one appears in the Machine Preset
        // picker on the next New Machine open.
        await discoverPresets()

        return (newPresetId, nil)
    }

    /// Delete a user preset.
    /// - Parameter preset: The preset to delete (must be a user preset)
    /// - Returns: true if deletion succeeded
    func deletePreset(_ preset: MachinePreset) async -> Bool {
        guard preset.isEditable else {
            NSLog("[PresetManager] Cannot delete system preset: \(preset.presetId)")
            return false
        }

        let (_, error) = await runCli(executable: preset.coreExecutablePath, arguments: ["delete-preset", preset.presetId])
        if let error = error {
            NSLog("[PresetManager] Failed to delete preset: \(error)")
            return false
        }

        NSLog("[PresetManager] Deleted preset: \(preset.presetId)")

        // Reload presets
        await discoverPresets()

        return true
    }

    /// Export a preset to a file.
    /// - Parameters:
    ///   - preset: The preset to export
    ///   - filepath: The destination file path
    /// - Returns: true if export succeeded
    func exportPreset(_ preset: MachinePreset, to filepath: String) async -> Bool {
        let (_, error) = await runCli(executable: preset.coreExecutablePath, arguments: ["export-preset", preset.presetId, "--output", filepath])
        if let error = error {
            NSLog("[PresetManager] Failed to export preset: \(error)")
            return false
        }

        NSLog("[PresetManager] Exported preset to: \(filepath)")
        return true
    }

    /// Import a preset from a file.
    /// - Parameters:
    ///   - filepath: The source file path
    ///   - executablePath: The executable to use for import (determines model)
    /// - Returns: The ID of the imported preset, or nil on failure
    func importPreset(from filepath: String, using executablePath: String) async -> String? {
        let (output, error) = await runCli(executable: executablePath, arguments: ["import-preset", filepath])
        if let error = error {
            NSLog("[PresetManager] Failed to import preset: \(error)")
            return nil
        }

        let newPresetId = output.trimmingCharacters(in: .whitespacesAndNewlines)
        NSLog("[PresetManager] Imported preset: \(newPresetId)")

        // Reload presets
        await discoverPresets()

        return newPresetId
    }

    /// Reveal a preset's directory in Finder.
    /// - Parameter preset: The preset to reveal
    func revealInFinder(_ preset: MachinePreset) {
        let dirpath = URL(fileURLWithPath: preset.presetFilepath).deletingLastPathComponent().path
        NSWorkspace.shared.selectFile(preset.presetFilepath, inFileViewerRootedAtPath: dirpath)
    }

    // MARK: - Core Launching

    /// Result of successfully launching a core process
    struct LaunchedCore {
        let process: Process
        let port: Int
        let provenanceUUID: String
    }

    /// Launch a core process for the given preset.
    /// The core is launched with --wait=api, meaning it waits for a Run() RPC before starting emulation.
    /// - Parameters:
    ///   - preset: The preset to launch
    ///   - storageConfig: Optional storage configuration with drive images
    /// - Returns: LaunchedCore on success, or error on failure
    func launchCore(_ preset: MachinePreset, storageConfig: StorageConfigurationState? = nil, memoryConfig: MemoryConfigurationState? = nil) async -> Result<LaunchedCore, CoreLaunchError> {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: preset.coreExecutablePath)

        let provenanceUUID = UUID().uuidString

        // Name the machine rather than letting the server fall back to the
        // model's display name, which would call every Model B the same thing
        // and leave several windows indistinguishable. The name belongs to
        // this instance, not to the preset, so it is decided here at launch
        // and can be changed afterwards.
        //
        // A number that fails to become a running machine is simply skipped;
        // nothing needs handing back, because numbers are never reused.
        let machineName = nameSequence.next(forPreset: preset.name)

        var arguments = [
            "start",
            "--preset", preset.presetFilepath,
            "--port", "0",
            "--advertise",
            "--wait=api",
            "--machine-name", machineName,
            "--provenance-type", "macos-gui",
            "--provenance-uuid", provenanceUUID
        ]

        // Pass ROM directory if running from app bundle with bundled ROMs
        if let romDir = bundledRomDirpath() {
            arguments.append(contentsOf: ["--rom-dir", romDir])
        }

        // Add floppy drive arguments from storage config
        if let config = storageConfig {
            for drive in config.drives {
                if let filepath = drive.imageFilepath {
                    arguments.append(contentsOf: ["--floppy", "\(drive.id):\(filepath)"])
                }
            }
        }

        // Add sideways arguments from memory config. Only sockets the user
        // changed emit anything; they override the preset's own sideways_bank
        // per slot on the server side.
        if let memoryConfig = memoryConfig {
            arguments.append(contentsOf: memoryConfig.sidewaysLaunchArguments())
        }

        process.arguments = arguments

        let stdoutPipe = Pipe()
        let stderrPipe = Pipe()
        process.standardOutput = stdoutPipe
        process.standardError = stderrPipe

        do {
            try process.run()
        } catch {
            return .failure(.launchFailed("Failed to launch: \(error.localizedDescription)"))
        }

        NSLog("[PresetManager] Launched \(preset.coreExecutablePath) with args: \(arguments.joined(separator: " "))")

        // Read stdout to find the port
        return await withCheckedContinuation { continuation in
            DispatchQueue.global(qos: .userInitiated).async {
                let handle = stdoutPipe.fileHandleForReading
                var buffer = Data()
                let portPattern = #"Listening on port (\d+)"#
                let regex = try? NSRegularExpression(pattern: portPattern)

                // Timeout after 5 seconds
                let deadline = Date().addingTimeInterval(5.0)

                while Date() < deadline {
                    // Check if process has exited unexpectedly
                    if !process.isRunning {
                        let stderrData = stderrPipe.fileHandleForReading.readDataToEndOfFile()
                        let stderrText = String(data: stderrData, encoding: .utf8)?.trimmingCharacters(in: .whitespacesAndNewlines) ?? "Unknown error"
                        let errorMsg = stderrText.isEmpty
                            ? "Process exited with status \(process.terminationStatus)"
                            : stderrText
                        continuation.resume(returning: .failure(.processExited(errorMsg)))
                        return
                    }

                    // Read available data
                    let chunk = handle.availableData
                    if chunk.isEmpty {
                        usleep(10_000) // 10ms
                        continue
                    }

                    buffer.append(chunk)

                    // Try to parse port from accumulated output
                    if let text = String(data: buffer, encoding: .utf8),
                       let match = regex?.firstMatch(in: text, range: NSRange(text.startIndex..., in: text)),
                       let portRange = Range(match.range(at: 1), in: text),
                       let port = Int(text[portRange]) {
                        NSLog("[PresetManager] Core listening on port \(port)")
                        continuation.resume(returning: .success(LaunchedCore(process: process, port: port, provenanceUUID: provenanceUUID)))
                        return
                    }
                }

                // Timeout - kill the process
                NSLog("[PresetManager] Timeout waiting for port, terminating process")
                process.terminate()
                continuation.resume(returning: .failure(.timeout))
            }
        }
    }

    // MARK: - CLI Execution

    /// Run a CLI command and return (stdout, errorMessage).
    private func runCli(executable: String, arguments: [String]) async -> (String, String?) {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: executable)
        process.arguments = arguments

        let stdoutPipe = Pipe()
        let stderrPipe = Pipe()
        process.standardOutput = stdoutPipe
        process.standardError = stderrPipe

        do {
            try process.run()
            process.waitUntilExit()

            let stdoutData = stdoutPipe.fileHandleForReading.readDataToEndOfFile()
            let stdout = String(data: stdoutData, encoding: .utf8) ?? ""

            guard process.terminationStatus == 0 else {
                let stderrData = stderrPipe.fileHandleForReading.readDataToEndOfFile()
                let stderrText = String(data: stderrData, encoding: .utf8)?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
                let errorMsg = stderrText.isEmpty
                    ? "exit status \(process.terminationStatus)"
                    : stderrText
                return (stdout, errorMsg)
            }

            return (stdout, nil)
        } catch {
            return ("", error.localizedDescription)
        }
    }
}
