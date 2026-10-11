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

    /// The typed wrapper over the server executable's command-line interface.
    /// Every run-to-completion subcommand goes through it; the long-lived
    /// `start` launch (which awaits a listening port) stays here, being a
    /// process-lifecycle concern rather than a run-and-decode one.
    private let coreCLI = CoreCLI()

    /// Disc image picker content types, fetched from the server once and kept
    /// for the session (disc format support does not change while the app
    /// runs).
    private var cachedDiscFormats: [DiscFormat]?

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

        guard let presetData = parsePresetFile(at: presetFilepath), let model = presetData.model else {
            configFailures.append(filename)
            NSLog("[PresetManager] Failed to parse preset file (or no model): \(filename)")
            return nil
        }

        let executablePath = "\(serversDirpath)/beebium-\(model)"
        guard FileManager.default.isExecutableFile(atPath: executablePath) else {
            NSLog("[PresetManager] Executable not found for model '\(model)': \(executablePath)")
            return nil
        }

        let (schema, error) = await fetchPresetSchema(from: executablePath)
        guard let schema = schema else {
            configFailures.append(filename)
            NSLog("[PresetManager] Failed to get schema from \(model): \(error ?? "unknown error")")
            return nil
        }

        let preset = MachinePreset(
            id: UUID(),
            presetId: presetId,
            name: presetData.name ?? schema.model.name,
            coreExecutablePath: executablePath,
            presetFilepath: presetFilepath,
            source: source,
            machineName: presetData.machineName,
            modelName: schema.model.name,
            modelDescription: presetData.description ?? schema.model.description,
            releaseDate: presetData.releaseDate
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

        // 2. App bundle (for distribution). The embedded payload is the static
        // server bundle tree copied verbatim under Resources/servers -- the same
        // bin/ lib/ share/beebium/ layout as the release tarball. The
        // executables (and their sibling extensions/ tree, which the server
        // resolves relative to its own location) live in servers/bin. The tree
        // is under Resources rather than Frameworks because it carries non-code
        // files (manifest.json, ROMs, presets) that codesign rejects in
        // Frameworks, which it validates as code-only. See the "Embed Server
        // Executables" build phase in project.yml.
        if let resourcePath = Bundle.main.resourcePath {
            let binPath = "\(resourcePath)/servers/bin"
            if FileManager.default.fileExists(atPath: "\(binPath)/beebium-model-b") {
                return binPath
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
        // ROMs ship inside the static server bundle tree, under
        // servers/share/beebium/roms (the same location the server binary finds
        // them relative to its own executable).
        let romPath = "\(resourcePath)/servers/share/beebium/roms"
        return FileManager.default.fileExists(atPath: romPath) ? romPath : nil
    }

    /// Get the bundled presets directory path, if presets are bundled in the app.
    /// Returns nil if no bundled presets are found.
    ///
    /// Presets ship inside the static server bundle tree, under
    /// servers/share/beebium/presets/ -- the same location the server binary's
    /// own PresetPaths::get_system_presets_dirpath() resolves relative to its
    /// executable, so `--from <id>` works. The Swift side reads the same directory.
    private func bundledPresetsDirpath() -> String? {
        guard let resourcePath = Bundle.main.resourcePath else { return nil }
        let presetsPath = "\(resourcePath)/servers/share/beebium/presets"
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

    /// Parse a preset file into a draft, for its discovery metadata (model,
    /// name, machine_name, description, release_date). The same model the dialog
    /// edits, so the client has a single preset representation.
    private func parsePresetFile(at filepath: String) -> MachineDraft? {
        guard let data = FileManager.default.contents(atPath: filepath) else {
            return nil
        }
        do {
            return try MachineDraft.decode(from: data)
        } catch {
            NSLog("[PresetManager] JSON decode error for \(filepath): \(error)")
            return nil
        }
    }

    /// Invoke `describe-preset-schema` on a server executable and parse the JSON response.
    /// Returns (schema, errorMessage) - errorMessage is nil on success.
    private func fetchPresetSchema(from executablePath: String) async -> (PresetSchema?, String?) {
        let (schema, error) = await coreCLI.presetSchema(executablePath: executablePath)
        if let error = error {
            NSLog("[PresetManager] Failed to get schema from \(executablePath): \(error)")
        }
        return (schema, error)
    }

    /// Fetch and decode the storage schema section for a preset's model.
    ///
    /// This queries the preset's core executable for its schema and extracts
    /// the storage section, which includes FDC options and floppy drive configuration.
    func fetchStorageSchema(for preset: MachinePreset) async -> StorageSchemaSection? {
        // One describe-preset-schema run, decoding the storage section; nil for
        // every reason the fetch can fail (the section decode logs its own).
        guard let section: StorageSchemaSection = await coreCLI.schemaSection(
            "storage", executablePath: preset.coreExecutablePath
        ) else {
            NSLog("[PresetManager] Failed to fetch storage schema for \(preset.presetId)")
            return nil
        }
        return section
    }

    /// Fetch the sideways_bank schema section for a preset's model.
    ///
    /// Describes the machine's physical sideways sockets (and aliasing) plus the
    /// ROMs it installs by default, used to build the Memory tab.
    func fetchSidewaysSchema(for preset: MachinePreset) async -> SidewaysSchemaSection? {
        return await coreCLI.schemaSection("sideways_bank", executablePath: preset.coreExecutablePath)
    }

    /// Parse a ROM image's header via the `describe-rom` subcommand, so the UI
    /// can show its real title/version. `image` is a ROM library name or path;
    /// returns nil if it can't be read or parsed.
    func fetchRomHeader(image: String, executablePath: String) async -> RomHeaderInfo? {
        // Resolve bare ROM names against the bundled ROM directory when present;
        // otherwise the server falls back to its own search path.
        await coreCLI.romHeader(image: image, executablePath: executablePath,
                                romDirpath: bundledRomDirpath())
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
    /// One disc format the server can load: its kind ("floppy" or "hard-disc")
    /// and the file extensions it accepts (bare, lowercased, no leading dot).
    struct DiscFormat: Equatable {
        let kind: String
        let extensions: [String]
    }

    /// The content types to filter a file picker to, for discs of `kind`
    /// ("floppy" or "hard-disc").
    ///
    /// Sourced from the server via list-disc-formats and cached for the
    /// session, so the client keeps no list of its own -- the picker offers
    /// exactly the formats the machine will load, and the two cannot drift.
    /// Empty (an unfiltered picker) if no executable can be asked or its
    /// output cannot be parsed, which is a better failure than a stale
    /// hardcoded list.
    func discImageContentTypes(kind: String) async -> [UTType] {
        await discFormats()
            .filter { $0.kind == kind }
            .flatMap { $0.extensions }
            .compactMap { UTType(filenameExtension: $0) }
    }

    /// Every disc format the server reports, cached for the session.
    private func discFormats() async -> [DiscFormat] {
        if let cached = cachedDiscFormats {
            return cached
        }
        guard let executablePath = anyCoreExecutablePath else {
            return []
        }
        // Cache only a real answer; a CLI error returns an unfiltered (empty)
        // picker this time and is retried next time.
        guard let formats = await coreCLI.discFormats(executablePath: executablePath) else {
            return []
        }
        cachedDiscFormats = formats
        return formats
    }

    /// The formats from list-disc-formats' JSONL output: each line's kind and
    /// its extensions (without the leading dot, lowercased). A pure,
    /// side-effect-free seam, so the parse can be tested without running a server.
    nonisolated static func parseDiscFormats(fromFormatList jsonl: String) -> [DiscFormat] {
        struct Line: Decodable { let kind: String; let extensions: [String] }
        var out: [DiscFormat] = []
        for raw in jsonl.split(whereSeparator: \.isNewline) {
            guard let data = raw.data(using: .utf8),
                  let line = try? JSONDecoder().decode(Line.self, from: data) else {
                continue
            }
            let exts = line.extensions.compactMap { ext -> String? in
                let bare = ext.hasPrefix(".") ? String(ext.dropFirst()) : ext
                return bare.isEmpty ? nil : bare.lowercased()
            }
            out.append(DiscFormat(kind: line.kind, extensions: exts))
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
        // A non-zero exit means the file could not be read at all; the
        // "not a disc image" verdict comes back as exit 0 with recognised
        // false, so it is not an error here.
        await coreCLI.describeDiscImage(path: path, executablePath: executablePath)
    }

    /// Fetch the `extensions` schema section for a preset's model: the model's
    /// attachment points and which extensions can attach where. Used to decide
    /// which configuration sections the model offers (e.g. hard discs).
    func fetchExtensionsSchema(for preset: MachinePreset) async -> ExtensionsSchemaSection? {
        await coreCLI.schemaSection("extensions", executablePath: preset.coreExecutablePath)
    }

    /// Fetch the `econet` schema section (station rules + transports) for a
    /// preset's model, or nil if the model has no Econet socket.
    func fetchEconetSchema(for preset: MachinePreset) async -> EconetSchemaSection? {
        await coreCLI.schemaSection("econet", executablePath: preset.coreExecutablePath)
    }

    /// Fetch the `startup` schema section (screen-mode / auto-boot / links rules)
    /// for a preset's model.
    func fetchStartupSchema(for preset: MachinePreset) async -> StartupSchemaSection? {
        await coreCLI.schemaSection("startup", executablePath: preset.coreExecutablePath)
    }

    /// Cached host serial-port paths for the session (for the choices_from
    /// "serial-ports" combo).
    private var cachedSerialPorts: [String]?

    /// The host's serial-port device paths, via `list-serial-ports`, as
    /// suggestions for a free-text device-path parameter. Cached for the session.
    func serialPortSuggestions() async -> [String] {
        if let cached = cachedSerialPorts { return cached }
        guard let executablePath = anyCoreExecutablePath else { return [] }
        // Cache only a real answer; a CLI error returns no suggestions this time
        // and is retried next time.
        guard let paths = await coreCLI.serialPorts(executablePath: executablePath) else { return [] }
        cachedSerialPorts = paths
        return paths
    }

    /// The device paths from list-serial-ports JSONL. A pure seam for testing.
    nonisolated static func parseSerialPortPaths(fromJSONL jsonl: String) -> [String] {
        struct Line: Decodable { let path: String }
        var out: [String] = []
        for raw in jsonl.split(whereSeparator: \.isNewline) {
            guard let data = raw.data(using: .utf8),
                  let line = try? JSONDecoder().decode(Line.self, from: data),
                  !line.path.isEmpty else { continue }
            out.append(line.path)
        }
        return out
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

        let (dirpath, error) = await coreCLI.reportPresetsDirpath(executablePath: executablePath)
        if let error = error {
            NSLog("[PresetManager] Failed to get user presets directory: \(error)")
            return nil
        }
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
        let (newPresetId, error) = await coreCLI.createPreset(
            name: newName, from: preset.presetId, executablePath: preset.coreExecutablePath)
        if let error = error {
            NSLog("[PresetManager] Failed to duplicate preset: \(error)")
            return nil
        }
        NSLog("[PresetManager] Created preset: \(newPresetId ?? "")")

        // Reload presets to pick up the new one
        await discoverPresets()

        return newPresetId
    }

    /// Delete a user preset.
    /// - Parameter preset: The preset to delete (must be a user preset)
    /// - Returns: true if deletion succeeded
    func deletePreset(_ preset: MachinePreset) async -> Bool {
        guard preset.isEditable else {
            NSLog("[PresetManager] Cannot delete system preset: \(preset.presetId)")
            return false
        }

        let error = await coreCLI.deletePreset(presetId: preset.presetId, executablePath: preset.coreExecutablePath)
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
        let error = await coreCLI.exportPreset(
            presetId: preset.presetId, to: filepath, executablePath: preset.coreExecutablePath)
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
        let (newPresetId, error) = await coreCLI.importPreset(filepath: filepath, executablePath: executablePath)
        if let error = error {
            NSLog("[PresetManager] Failed to import preset: \(error)")
            return nil
        }
        NSLog("[PresetManager] Imported preset: \(newPresetId ?? "")")

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

    /// Launch a core process for the given preset, as written (no per-setting
    /// overrides). The configurable New Machine flow launches an edited draft
    /// via `launchCore(draft:executablePath:)`; this is the quick-launch of a
    /// preset exactly as it stands, used from the welcome window.
    ///
    /// The core is launched with --wait=api, meaning it waits for a Run() RPC
    /// before starting emulation.
    /// - Returns: LaunchedCore on success, or error on failure
    func launchCore(_ preset: MachinePreset) async -> Result<LaunchedCore, CoreLaunchError> {
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
        //
        // The name is a template: the preset's own `machine_name` when it has
        // one, otherwise its display name with braces escaped, ending in the
        // {machine-ordinal} placeholder. The server renders the placeholders
        // (e.g. {econet-station}, {machine-ordinal}) and re-renders them as that
        // state changes; the ordinal's value is supplied below.
        let ordinal = nameSequence.nextOrdinal(forPreset: preset.name)
        let machineName = MachineLaunchName.template(
            presetMachineName: preset.machineName,
            presetDisplayName: preset.name)

        var arguments = [
            "start",
            "--preset", preset.presetFilepath,
            "--port", "0",
            "--advertise",
            "--wait=api",
            "--machine-name", machineName,
            "--machine-ordinal", "\(ordinal)",
            "--provenance-type", "macos-gui",
            "--provenance-uuid", provenanceUUID
        ]

        // Pass ROM directory if running from app bundle with bundled ROMs
        if let romDir = bundledRomDirpath() {
            arguments.append(contentsOf: ["--rom-dir", romDir])
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

        return await Self.awaitListeningPort(process: process,
                                             stdoutPipe: stdoutPipe,
                                             stderrPipe: stderrPipe,
                                             provenanceUUID: provenanceUUID)
    }

    // MARK: - Preset resolution

    /// A diagnostic from `resolve-preset`: an error or warning about the preset,
    /// with a JSON-pointer `path` into the document.
    struct PresetDiagnostic: Decodable, Equatable {
        let severity: String   // "error" | "warning"
        let path: String
        let message: String
        /// A human description of where the diagnostic points, in the schema's own
        /// words (e.g. "Floppy drive 0", "Sideways slot 13", an extension's display
        /// name), produced by resolve-preset. Empty for a whole-document diagnostic.
        let location: String
        var isError: Bool { severity == "error" }
    }

    /// Resolve a preset to its fully-normalised draft via the server's
    /// `resolve-preset` subcommand -- defaults filled in, paths made absolute,
    /// the model's default ROMs expanded into explicit slots. This is the oracle
    /// the New Machine dialog edits, so the client never has to parse or
    /// second-guess a preset file itself.
    ///
    /// Returns the resolved draft (nil when nothing could be loaded) and any
    /// diagnostics; `resolve-preset` prints its JSON even on a non-zero exit, so
    /// the diagnostics are parsed regardless.
    func resolvePreset(_ preset: MachinePreset) async -> (MachineDraft?, [PresetDiagnostic]) {
        await resolve(presetPath: preset.presetFilepath, executablePath: preset.coreExecutablePath)
    }

    /// Validate/normalise an in-memory draft: write it to a temporary preset file
    /// and resolve that. Used to re-validate the dialog's draft as the user edits
    /// it, so diagnostics (and whether launching is allowed) reflect the current
    /// configuration, not just the preset as loaded.
    func resolveDraft(_ draft: MachineDraft, executablePath: String) async -> (MachineDraft?, [PresetDiagnostic]) {
        guard let url = try? Self.writeTemporaryPreset(draft) else { return (nil, []) }
        defer { try? FileManager.default.removeItem(at: url) }
        return await resolve(presetPath: url.path, executablePath: executablePath)
    }

    private func resolve(presetPath: String, executablePath: String) async -> (MachineDraft?, [PresetDiagnostic]) {
        await coreCLI.resolvePreset(presetPath: presetPath, executablePath: executablePath)
    }

    // MARK: - Draft-based launching

    /// Launch a core from a fully-specified `MachineDraft`.
    ///
    /// The draft is the whole machine configuration: it is serialised to a
    /// temporary preset file and the core is started with `start --preset
    /// <temp>` plus server plumbing only (port, advertise, wait, machine
    /// name/ordinal, provenance, rom-dir). No per-setting CLI overrides -- the
    /// preset document carries everything -- so there is a single
    /// representation of a machine's configuration.
    ///
    /// The temporary file is removed when the launched process terminates, which
    /// is unambiguously after the server has read it; relative file paths in a
    /// draft resolve against this temp directory for now, which is why shipped
    /// presets use bare (search-path) ROM names and the user's floppy paths are
    /// absolute. Once `resolve-preset` becomes the oracle, every path the draft
    /// carries is absolute and this caveat is moot.
    func launchCore(draft: MachineDraft, executablePath: String) async -> Result<LaunchedCore, CoreLaunchError> {
        let provenanceUUID = UUID().uuidString
        let displayName = draft.name ?? ""
        let ordinal = nameSequence.nextOrdinal(forPreset: displayName)
        let machineName = MachineLaunchName.template(
            presetMachineName: draft.machineName, presetDisplayName: displayName)

        let presetURL: URL
        do {
            presetURL = try Self.writeTemporaryPreset(draft)
        } catch {
            return .failure(.launchFailed("Failed to write temporary preset: \(error.localizedDescription)"))
        }

        let process = Process()
        process.executableURL = URL(fileURLWithPath: executablePath)
        process.arguments = Self.draftLaunchArguments(
            presetFilepath: presetURL.path,
            machineName: machineName,
            ordinal: ordinal,
            provenanceUUID: provenanceUUID,
            romDirpath: bundledRomDirpath())

        // Remove the temporary preset once the process ends -- after every read
        // of it, whether the launch succeeded, failed or timed out.
        process.terminationHandler = { _ in
            try? FileManager.default.removeItem(at: presetURL)
        }

        let stdoutPipe = Pipe()
        let stderrPipe = Pipe()
        process.standardOutput = stdoutPipe
        process.standardError = stderrPipe

        do {
            try process.run()
        } catch {
            try? FileManager.default.removeItem(at: presetURL)
            return .failure(.launchFailed("Failed to launch: \(error.localizedDescription)"))
        }

        NSLog("[PresetManager] Launched \(executablePath) with args: \(process.arguments?.joined(separator: " ") ?? "")")

        return await Self.awaitListeningPort(process: process,
                                             stdoutPipe: stdoutPipe,
                                             stderrPipe: stderrPipe,
                                             provenanceUUID: provenanceUUID)
    }

    /// The `start` arguments for launching a draft preset file: server plumbing
    /// only, no per-setting overrides. Pure, so the argument shape is testable
    /// without a launch.
    nonisolated static func draftLaunchArguments(presetFilepath: String,
                                                 machineName: String,
                                                 ordinal: Int,
                                                 provenanceUUID: String,
                                                 romDirpath: String?) -> [String] {
        var arguments = [
            "start",
            "--preset", presetFilepath,
            "--port", "0",
            "--advertise",
            "--wait=api",
            "--machine-name", machineName,
            "--machine-ordinal", "\(ordinal)",
            "--provenance-type", "macos-gui",
            "--provenance-uuid", provenanceUUID,
        ]
        if let romDirpath = romDirpath {
            arguments.append(contentsOf: ["--rom-dir", romDirpath])
        }
        return arguments
    }

    /// Serialise a draft to a fresh temporary `*.preset.beebium` file and return
    /// its URL. Each call returns a distinct path.
    nonisolated static func writeTemporaryPreset(_ draft: MachineDraft) throws -> URL {
        let dir = FileManager.default.temporaryDirectory
            .appendingPathComponent("Beebium", isDirectory: true)
            .appendingPathComponent("drafts", isDirectory: true)
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let url = dir.appendingPathComponent("\(UUID().uuidString).preset.beebium")
        try draft.encoded().write(to: url, options: .atomic)
        return url
    }

    /// Wait for a freshly-launched core to print its listening port, or fail.
    ///
    /// The core is launched with `--port 0` and prints "Listening on port N";
    /// this reads stdout until that line, the process exits, or a timeout.
    private static func awaitListeningPort(process: Process,
                                           stdoutPipe: Pipe,
                                           stderrPipe: Pipe,
                                           provenanceUUID: String) async -> Result<LaunchedCore, CoreLaunchError> {
        await withCheckedContinuation { continuation in
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

    /// Save a draft as a new user preset by writing it to a temporary file and
    /// importing it through the server's `import-preset` subcommand, so the
    /// server owns writing the user-presets directory (the client never writes
    /// it). The whole draft is persisted -- FDC, floppies and sideways alike --
    /// not just a subset.
    ///
    /// Returns `(presetId, errorMessage)` with exactly one non-nil.
    func saveDraftAsPreset(_ draft: MachineDraft,
                           name: String,
                           executablePath: String) async -> (String?, String?) {
        var named = draft
        named.name = name
        let url: URL
        do {
            url = try Self.writeTemporaryPreset(named)
        } catch {
            return (nil, "Failed to write preset: \(error.localizedDescription)")
        }
        defer { try? FileManager.default.removeItem(at: url) }

        let (newPresetId, error) = await coreCLI.importPreset(filepath: url.path, executablePath: executablePath)
        if let error = error {
            NSLog("[PresetManager] Failed to import preset: \(error)")
            return (nil, error.trimmingCharacters(in: .whitespacesAndNewlines))
        }
        NSLog("[PresetManager] Saved draft as preset: \(newPresetId ?? "")")

        // Reload presets so the new one appears in the picker next time.
        await discoverPresets()
        return (newPresetId, nil)
    }
}

/// A typed wrapper over the server executable's command-line interface. One
/// method per run-to-completion subcommand the client invokes: each builds the
/// arguments, runs the executable with `run(...)`, and decodes its output,
/// mapping a non-zero exit (or unparseable output) to the method's own failure
/// shape. PresetManager owns one and routes every such call through it, so
/// process invocation and output decoding live in one place rather than being
/// hand-rolled at each call site.
///
/// The long-lived `start` launch (which spawns a server and awaits its
/// listening port) is not here: it is a process-lifecycle concern, not a
/// run-and-decode one, and stays in PresetManager. The two tiny JSONL line
/// parsers (`PresetManager.parseDiscFormats`, `parseSerialPortPaths`) stay there
/// too, with their pinned-sample tests; this wrapper calls them.
///
/// `@MainActor` so `run(...)`'s synchronous `waitUntilExit()` blocks the same
/// actor it did when this was PresetManager.runCli, keeping the threading
/// behaviour unchanged.
@MainActor
struct CoreCLI {
    /// Run `executablePath` with `arguments` to completion, returning its stdout
    /// and, on a non-zero exit (or a spawn failure), an error message (the
    /// trimmed stderr, or a status/thrown description).
    func run(executablePath: String, arguments: [String]) async -> (stdout: String, error: String?) {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: executablePath)
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

    // MARK: - Schema

    /// `describe-preset-schema`, decoded whole as a PresetSchema. Returns
    /// (schema, errorMessage) with the error nil on success.
    func presetSchema(executablePath: String) async -> (PresetSchema?, String?) {
        let (output, error) = await run(executablePath: executablePath, arguments: ["describe-preset-schema"])
        if let error = error { return (nil, error) }
        guard let data = output.data(using: .utf8) else {
            return (nil, "no output")
        }
        do {
            return (try JSONDecoder().decode(PresetSchema.self, from: data), nil)
        } catch {
            return (nil, error.localizedDescription)
        }
    }

    /// `describe-preset-schema`, decoding the one section whose `type` matches as
    /// `T`. nil when the executable could not be run, the section is absent, or it
    /// does not decode.
    func schemaSection<T: Decodable>(_ type: String, executablePath: String) async -> T? {
        let (output, error) = await run(executablePath: executablePath, arguments: ["describe-preset-schema"])
        if error != nil { return nil }
        guard let data = output.data(using: .utf8) else { return nil }
        do {
            guard let json = try JSONSerialization.jsonObject(with: data) as? [String: Any],
                  let sections = json["sections"] as? [[String: Any]],
                  let section = sections.first(where: { ($0["type"] as? String) == type }) else {
                return nil
            }
            let sectionData = try JSONSerialization.data(withJSONObject: section)
            return try JSONDecoder().decode(T.self, from: sectionData)
        } catch {
            NSLog("[CoreCLI] Failed to decode '\(type)' schema section: \(error)")
            return nil
        }
    }

    // MARK: - ROMs and disc images

    /// `describe-rom <image>`, resolving bare names against `romDirpath` when
    /// given. nil on any failure to run or decode.
    func romHeader(image: String, executablePath: String, romDirpath: String?) async -> RomHeaderInfo? {
        var arguments = ["describe-rom", image]
        if let romDirpath = romDirpath {
            arguments.append(contentsOf: ["--rom-dir", romDirpath])
        }
        let (output, error) = await run(executablePath: executablePath, arguments: arguments)
        if error != nil { return nil }
        guard let data = output.data(using: .utf8),
              let info = try? JSONDecoder().decode(RomHeaderInfo.self, from: data) else {
            return nil
        }
        return info
    }

    /// `describe-disc-image <path>`. nil only when the executable could not be
    /// run at all; a "not a disc image" verdict comes back as a decoded
    /// DiscImageInfo with recognised == false.
    func describeDiscImage(path: String, executablePath: String) async -> DiscImageInfo? {
        let (output, error) = await run(executablePath: executablePath, arguments: ["describe-disc-image", path])
        if error != nil { return nil }
        guard let data = output.data(using: .utf8),
              let info = try? JSONDecoder().decode(DiscImageInfo.self, from: data) else {
            return nil
        }
        return info
    }

    /// `list-disc-formats` (JSONL), parsed to the disc formats the server loads.
    /// nil when the executable could not be run (so the caller can retry rather
    /// than cache a failure); an empty array is a real answer.
    func discFormats(executablePath: String) async -> [PresetManager.DiscFormat]? {
        let (output, error) = await run(
            executablePath: executablePath, arguments: ["--format", "jsonl", "list-disc-formats"])
        if error != nil { return nil }
        return PresetManager.parseDiscFormats(fromFormatList: output)
    }

    /// `list-serial-ports` (JSONL), parsed to the host's device paths. nil when
    /// the executable could not be run (so the caller can retry); an empty array
    /// is a real answer.
    func serialPorts(executablePath: String) async -> [String]? {
        let (output, error) = await run(
            executablePath: executablePath, arguments: ["--format", "jsonl", "list-serial-ports"])
        if error != nil { return nil }
        return PresetManager.parseSerialPortPaths(fromJSONL: output)
    }

    // MARK: - Preset resolution

    /// `resolve-preset <path> --json`, decoded to the resolved draft and its
    /// diagnostics. resolve-preset prints its JSON even on a non-zero exit, so
    /// the error is ignored and the output parsed regardless; (nil, []) when
    /// nothing could be parsed.
    func resolvePreset(presetPath: String, executablePath: String) async -> (MachineDraft?, [PresetManager.PresetDiagnostic]) {
        let (output, _) = await run(
            executablePath: executablePath, arguments: ["resolve-preset", presetPath, "--json"])
        return Self.decodeResolveOutput(output) ?? {
            NSLog("[CoreCLI] Could not parse resolve-preset output for \(presetPath)")
            return (nil, [])
        }()
    }

    /// Decode `resolve-preset --json` output: the resolved preset (optional) and
    /// its diagnostics. nil when the text is not the expected JSON. A pure seam,
    /// so the decode is testable against pinned samples without running a server.
    nonisolated static func decodeResolveOutput(_ text: String) -> (MachineDraft?, [PresetManager.PresetDiagnostic])? {
        struct Output: Decodable {
            let preset: MachineDraft?
            let diagnostics: [PresetManager.PresetDiagnostic]
        }
        guard let data = text.data(using: .utf8),
              let parsed = try? JSONDecoder().decode(Output.self, from: data) else {
            return nil
        }
        return (parsed.preset, parsed.diagnostics)
    }

    // MARK: - Preset management

    /// `report-presets-dirpath`, trimmed. Returns (dirpath, errorMessage) with
    /// exactly one non-nil.
    func reportPresetsDirpath(executablePath: String) async -> (String?, String?) {
        let (output, error) = await run(executablePath: executablePath, arguments: ["report-presets-dirpath"])
        if let error = error { return (nil, error) }
        return (output.trimmingCharacters(in: .whitespacesAndNewlines), nil)
    }

    /// `create-preset --name <name> --from <presetId>`. Returns (newPresetId,
    /// errorMessage) with exactly one non-nil; the id is trimmed.
    func createPreset(name: String, from presetId: String, executablePath: String) async -> (String?, String?) {
        let (output, error) = await run(
            executablePath: executablePath, arguments: ["create-preset", "--name", name, "--from", presetId])
        if let error = error { return (nil, error) }
        return (output.trimmingCharacters(in: .whitespacesAndNewlines), nil)
    }

    /// `delete-preset <presetId>`. Returns an error message, or nil on success.
    func deletePreset(presetId: String, executablePath: String) async -> String? {
        let (_, error) = await run(executablePath: executablePath, arguments: ["delete-preset", presetId])
        return error
    }

    /// `export-preset <presetId> --output <filepath>`. Returns an error message,
    /// or nil on success.
    func exportPreset(presetId: String, to filepath: String, executablePath: String) async -> String? {
        let (_, error) = await run(
            executablePath: executablePath, arguments: ["export-preset", presetId, "--output", filepath])
        return error
    }

    /// `import-preset <filepath>`. Returns (newPresetId, errorMessage) with
    /// exactly one non-nil; the id is trimmed.
    func importPreset(filepath: String, executablePath: String) async -> (String?, String?) {
        let (output, error) = await run(executablePath: executablePath, arguments: ["import-preset", filepath])
        if let error = error { return (nil, error) }
        return (output.trimmingCharacters(in: .whitespacesAndNewlines), nil)
    }
}
