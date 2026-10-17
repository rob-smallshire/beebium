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

/// Manages machine presets discovered from the server's `list-presets` listing.
///
/// Locates the server executables via:
/// 1. `BEEBIUM_SERVERS_DIRPATH` environment variable (if set)
/// 2. Fallback to `~/Code/beebium/build/src/server/` for development
///
/// Each listing row names the preset's model, which maps to an executable
/// (beebium-{model}); the server owns the stored form (a .beebium-preset archive).
@MainActor
class PresetManager: ObservableObject {
    static let shared = PresetManager()

    @Published private(set) var systemPresets: [MachinePreset] = []
    @Published private(set) var userPresets: [MachinePreset] = []
    @Published private(set) var isDiscovering = false
    @Published private(set) var discoveryError: String?

    /// Model id -> model display name (e.g. "model-b" -> "BBC Model B"), from
    /// describe-preset-schema, kept for the app's lifetime so a model's name is
    /// fetched at most once however many presets name it or how often presets
    /// are rediscovered.
    private var cachedModelDisplayNames: [String: String] = [:]

    /// Numbers machines per preset, for this run of the app only.
    private let nameSequence = MachineNameSequence()

    /// The session's preset thumbnails, fetched lazily from the server via
    /// show-preset and kept in memory. The views ask this rather than reading
    /// a thumbnail file themselves.
    let thumbnailCache = PresetThumbnailCache()

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

    /// Discover presets from the server's listing and build the system and user
    /// lists. The server is the single source of truth for what presets exist
    /// and what they are called, so this reads `list-presets --all-models`
    /// rather than opening preset files: one executable reports every model's
    /// presets, system and user alike, each row carrying its id, display name,
    /// source, model, description and stored path.
    func discoverPresets() async {
        isDiscovering = true
        discoveryError = nil
        systemPresets = []
        userPresets = []

        let dirpath = serversDirpath()
        let listingExecutablePath = "\(dirpath)/beebium-model-b"
        guard FileManager.default.isExecutableFile(atPath: listingExecutablePath) else {
            NSLog("[PresetManager] No server executable to list presets at: \(listingExecutablePath)")
            isDiscovering = false
            discoveryError = "No server executable found in \(dirpath)"
            return
        }

        let (rows, error) = await coreCLI.listPresets(allModels: true, executablePath: listingExecutablePath)
        if let error = error {
            NSLog("[PresetManager] list-presets failed: \(error)")
            isDiscovering = false
            discoveryError = "Could not list presets: \(error)"
            return
        }
        NSLog("[PresetManager] Listed \(rows.count) preset(s)")

        // Resolve each distinct model's display name once (for the "Based on"
        // label), rather than once per preset as the old file-by-file discovery
        // did; the preset's own name and description come from the row.
        let modelNames = await modelDisplayNames(for: rows, serversDirpath: dirpath,
                                                 listingExecutablePath: listingExecutablePath)

        let catalog = PresetCatalog.build(
            rows: rows,
            serversDirpath: dirpath,
            listingExecutablePath: listingExecutablePath,
            executableExists: { FileManager.default.isExecutableFile(atPath: $0) },
            modelDisplayName: { modelNames[$0.model ?? ""] ?? "" })

        systemPresets = sortPresets(catalog.system)
        userPresets = sortPresets(catalog.user)
        isDiscovering = false

        // One line per catalogue build naming the counts, so a launch can be
        // confirmed to have discovered presets (the app-launch CI step greps
        // for this).
        NSLog("[PresetManager] Preset catalogue: \(systemPresets.count) system, \(userPresets.count) user")

        if !catalog.skipped.isEmpty {
            NSLog("[PresetManager] Skipped \(catalog.skipped.count) preset(s) with no matching executable: \(catalog.skipped.joined(separator: ", "))")
        }

        if systemPresets.isEmpty && userPresets.isEmpty {
            discoveryError = rows.isEmpty
                ? "No presets found"
                : "Found \(rows.count) preset(s) but no matching server executable"
        }
    }

    /// The display name of each distinct model among `rows` (e.g. "model-b" ->
    /// "BBC Model B"), from `describe-preset-schema`, fetched once per model and
    /// cached for the app's lifetime. A row naming no model is keyed by "" and
    /// resolved against the listing executable. The map's value is "" for a
    /// model whose executable is absent or whose schema cannot be read.
    private func modelDisplayNames(for rows: [PresetListing], serversDirpath: String,
                                   listingExecutablePath: String) async -> [String: String] {
        var names: [String: String] = [:]
        for model in Set(rows.map { $0.model ?? "" }) {
            if let cached = cachedModelDisplayNames[model] {
                names[model] = cached
                continue
            }
            let executablePath = model.isEmpty
                ? listingExecutablePath
                : "\(serversDirpath)/beebium-\(model)"
            guard FileManager.default.isExecutableFile(atPath: executablePath) else { continue }
            let (schema, _) = await coreCLI.presetSchema(executablePath: executablePath)
            let name = schema?.model.name ?? ""
            cachedModelDisplayNames[model] = name
            names[model] = name
        }
        return names
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

    /// Import one or more presets, each a `.beebium-preset` archive, a
    /// directory laid out like one, or a lone JSON preset, then rediscover
    /// once. import-preset validates each file and names its model itself, so
    /// any one executable serves whatever model a preset is for.
    /// - Returns: the file names that failed, empty when all succeeded.
    func importPresets(from filepaths: [String], using executablePath: String) async -> [String] {
        var failures: [String] = []
        for filepath in filepaths {
            let (newPresetId, error) = await coreCLI.importPreset(filepath: filepath, executablePath: executablePath)
            if let error = error {
                NSLog("[PresetManager] Failed to import \(filepath): \(error)")
                failures.append(URL(fileURLWithPath: filepath).lastPathComponent)
            } else {
                NSLog("[PresetManager] Imported preset: \(newPresetId ?? "")")
            }
        }
        await discoverPresets()
        return failures
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
        let provenanceUUID = UUID().uuidString

        // Give the server this instance's ordinal so the default name it builds
        // from the preset's name reads "Model B (Disc) #2" rather than every
        // Model B being called the same thing. The name belongs to this
        // instance, not the preset, and can be changed afterwards; the server
        // owns how the default is composed, so no name is passed here.
        //
        // A number that fails to become a running machine is simply skipped;
        // nothing needs handing back, because numbers are never reused.
        let ordinal = nameSequence.nextOrdinal(forPreset: preset.name)

        let arguments = Self.presetLaunchArguments(
            presetFilepath: preset.presetFilepath,
            ordinal: ordinal,
            provenanceUUID: provenanceUUID,
            romDirpath: bundledRomDirpath())

        return await spawnCore(executablePath: preset.coreExecutablePath,
                               arguments: arguments,
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

        let presetURL: URL
        do {
            presetURL = try Self.writeTemporaryPreset(draft)
        } catch {
            return .failure(.launchFailed("Failed to write temporary preset: \(error.localizedDescription)"))
        }

        let arguments = Self.presetLaunchArguments(
            presetFilepath: presetURL.path,
            ordinal: ordinal,
            provenanceUUID: provenanceUUID,
            romDirpath: bundledRomDirpath())

        // Remove the temporary preset once the process ends -- after every read
        // of it, whether the launch succeeded, failed or timed out. spawnCore
        // guarantees the handler runs exactly once: as the termination handler
        // of a process that started, or directly if the spawn itself threw.
        return await spawnCore(executablePath: executablePath,
                               arguments: arguments,
                               provenanceUUID: provenanceUUID,
                               onTerminate: { _ in
                                   try? FileManager.default.removeItem(at: presetURL)
                               })
    }

    // MARK: - The one place a core process is spawned

    /// Spawn a core process with the given arguments and wait for it to announce
    /// its listening port. Every launch -- a preset file, an edited draft, and
    /// (from the saved-machines work) an existing machine directory -- builds
    /// its own argument list and hands it here, so the process creation, the
    /// pipes and the port-wait are written once rather than per launch kind.
    ///
    /// `onTerminate`, when given, is run exactly once after the process can
    /// produce no further reads: as the process's termination handler if it
    /// started, or directly if `run()` threw. The draft launch uses it to
    /// delete its temporary preset file.
    private func spawnCore(executablePath: String,
                           arguments: [String],
                           provenanceUUID: String,
                           onTerminate: (@Sendable (Process) -> Void)? = nil)
        async -> Result<LaunchedCore, CoreLaunchError> {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: executablePath)
        process.arguments = arguments
        if let onTerminate {
            process.terminationHandler = onTerminate
        }

        let stdoutPipe = Pipe()
        let stderrPipe = Pipe()
        process.standardOutput = stdoutPipe
        process.standardError = stderrPipe

        do {
            try process.run()
        } catch {
            // The termination handler never fires for a process that did not
            // start, so run the cleanup here instead to keep it exactly-once.
            onTerminate?(process)
            return .failure(.launchFailed("Failed to launch: \(error.localizedDescription)"))
        }

        NSLog("[PresetManager] Launched \(executablePath) with args: \(arguments.joined(separator: " "))")

        return await Self.awaitListeningPort(process: process,
                                             stdoutPipe: stdoutPipe,
                                             stderrPipe: stderrPipe,
                                             provenanceUUID: provenanceUUID)
    }

    /// The `start` arguments for launching a preset file (a quick-launch of a
    /// stored preset, or an edited draft written to a temporary file): server
    /// plumbing only, no per-setting overrides. No `--machine-name`: the server
    /// composes the default name from the preset's own name, and `--machine-
    /// ordinal` gives it this instance's number so several windows from one
    /// preset read "#1", "#2". The preset carries any `machine_description`, so
    /// the subtitle needs no argument either. Pure, so the argument shape is
    /// testable without a launch.
    nonisolated static func presetLaunchArguments(presetFilepath: String,
                                                  ordinal: Int,
                                                  provenanceUUID: String,
                                                  romDirpath: String?) -> [String] {
        var arguments = [
            "start",
            "--preset", presetFilepath,
            "--port", "0",
            "--advertise",
            "--wait=api",
            "--machine-ordinal", "\(ordinal)",
            "--provenance-type", "macos-gui",
            "--provenance-uuid", provenanceUUID,
        ]
        if let romDirpath = romDirpath {
            arguments.append(contentsOf: ["--rom-dir", romDirpath])
        }
        return arguments
    }

    /// The `start` arguments for opening an existing saved machine directory:
    /// server plumbing only. Unlike a preset launch this carries no
    /// `--machine-name` or `--machine-ordinal`: a saved machine's name and
    /// identity live in its `machine.json`, which the server reads and reports,
    /// so the app supplies only the plumbing it needs to own the process. Pure,
    /// so the argument shape is testable without a launch.
    nonisolated static func machineLaunchArguments(machineDirpath: String,
                                                   provenanceUUID: String,
                                                   romDirpath: String?) -> [String] {
        var arguments = [
            "start",
            "--machine", machineDirpath,
            "--port", "0",
            "--advertise",
            "--wait=api",
            "--provenance-type", "macos-gui",
            "--provenance-uuid", provenanceUUID,
        ]
        if let romDirpath = romDirpath {
            arguments.append(contentsOf: ["--rom-dir", romDirpath])
        }
        return arguments
    }

    // MARK: - Saved machines

    /// Describe a saved machine directory (or its document file) via
    /// `describe-machine`. Any core executable serves -- the subcommand reads
    /// the directory, not a running server. nil before presets have been
    /// discovered (no executable to run) or when the directory could not be read.
    func describeMachine(path: String) async -> MachineDescription? {
        guard let executablePath = anyCoreExecutablePath else { return nil }
        return await coreCLI.describeMachine(path: path, executablePath: executablePath)
    }

    /// Open an existing saved machine directory: launch a core on it with
    /// `start --machine`. The executable must match the machine's model (the
    /// server refuses another model), so the caller passes the model from
    /// describe-machine. Like launchCore(_:) but with no name or ordinal -- a
    /// saved machine's name lives in its machine.json.
    func launchMachine(machineDirpath: String, model: String) async -> Result<LaunchedCore, CoreLaunchError> {
        let provenanceUUID = UUID().uuidString
        let arguments = Self.machineLaunchArguments(
            machineDirpath: machineDirpath,
            provenanceUUID: provenanceUUID,
            romDirpath: bundledRomDirpath())
        return await spawnCore(executablePath: coreExecutablePath(forModel: model),
                               arguments: arguments,
                               provenanceUUID: provenanceUUID)
    }

    /// The server executable for `model`, under the servers directory
    /// (`beebium-<model>`).
    func coreExecutablePath(forModel model: String) -> String {
        "\(serversDirpath())/beebium-\(model)"
    }

    /// Serialise a draft to a fresh temporary JSON file and return its URL.
    /// Each call returns a distinct path. The name carries a neutral `.json`
    /// extension rather than a preset suffix: the server reads this as a lone
    /// JSON preset for resolve-preset, start --preset and import-preset
    /// whatever it is called, and the stored preset form is the server's to
    /// decide, not the client's.
    nonisolated static func writeTemporaryPreset(_ draft: MachineDraft) throws -> URL {
        let dir = FileManager.default.temporaryDirectory
            .appendingPathComponent("Beebium", isDirectory: true)
            .appendingPathComponent("drafts", isDirectory: true)
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let url = dir.appendingPathComponent("\(UUID().uuidString).json")
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
