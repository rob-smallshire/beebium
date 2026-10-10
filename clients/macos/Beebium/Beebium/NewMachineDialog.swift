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

import SwiftUI

struct NewMachineDialog: View {
    @Environment(\.dismiss) private var dismiss
    @Environment(\.openWindow) private var openWindow

    @StateObject private var presetManager = PresetManager.shared
    @ObservedObject private var windowState = ConnectWindowState.shared

    // Preset selection - persisted via @AppStorage
    @AppStorage("lastSelectedPresetId") private var lastSelectedPresetId: String = ""
    @State private var selectedPreset: MachinePreset?

    // Configuration state
    /// The draft preset document being edited: the single source of truth for
    /// every configuration section, launched and saved as-is.
    @State private var draft = MachineDraft()
    /// The draft as first loaded for the selected preset, so sections can show
    /// what the user changed.
    @State private var originalDraft = MachineDraft()
    @State private var storageSchema: StorageSchemaSection?
    @State private var memorySchema: SidewaysSchemaSection?
    /// The model's hard-disc capability (adapter + disc chain), if it has one.
    @State private var hardDiscCapability: HardDiscCapability?
    /// The model's extensions schema (attachment points), for Peripherals.
    @State private var extensionsSchema: ExtensionsSchemaSection?
    /// The model's econet schema (station rules + transports), for Network.
    @State private var econetSchema: EconetSchemaSection?
    /// The model's startup schema, for Keyboard.
    @State private var startupSchema: StartupSchemaSection?
    /// Host serial-port paths, for choices_from "serial-ports" fields.
    @State private var serialPorts: [String] = []
    @State private var isLoadingSchema = false

    /// resolve-preset diagnostics for the current draft. Errors block launching;
    /// re-evaluated (debounced) as the draft changes.
    @State private var diagnostics: [PresetManager.PresetDiagnostic] = []
    @State private var validationTask: Task<Void, Never>?

    // Save as preset
    @State private var saveAsNewPreset = false
    @State private var newPresetName = ""

    // Launch state
    @State private var isLaunching = false
    @State private var launchError: String?

    // The hosting NSWindow, captured via WindowAccessor so we can give a
    // freshly-opened dialog its natural height. The window stays
    // user-resizable; we just set a sensible baseline on each open.
    @State private var hostingWindow: NSWindow?

    private let dialogWidth: CGFloat = 520

    // The dialog's natural content height: tuned so it fits cleanly with no
    // scrollbar and no large gap below the button bar.
    private let contentHeight: CGFloat = 560

    var body: some View {
        VStack(spacing: 0) {
            // Content
            VStack(alignment: .leading, spacing: 16) {
                presetPickerSection
                descriptionSection
                configurationSection
                saveAsPresetSection
                PresetDiagnosticsView(diagnostics: diagnostics)
                errorSection
            }
            .padding(20)

            Divider()

            // Buttons
            buttonBar
        }
        .frame(minWidth: dialogWidth, maxWidth: .infinity)
        .background(WindowAccessor(window: $hostingWindow))
        .task {
            // First-open initial setup. The dialog window is a singleton:
            // SwiftUI doesn't re-fire .task when the window is re-shown
            // after dismiss(), so anything that has to happen on EVERY
            // open goes in the willCloseNotification reset path below.
            if presetManager.systemPresets.isEmpty {
                await presetManager.discoverPresets()
            }
            restoreLastSelection()
            if let preset = selectedPreset {
                loadDraftForPreset(preset)
            }
        }
        // Reset transient state when the dialog closes so the next
        // open is fresh. .task on a singleton Window doesn't re-fire,
        // so we hook AppKit's close notification - it fires for every
        // dismiss path (Cancel button, Create-success, red close
        // button, Cmd-W) regardless of how the dialog goes away.
        .onReceive(NotificationCenter.default
            .publisher(for: NSWindow.willCloseNotification)) { note in
            guard let window = note.object as? NSWindow,
                  window === hostingWindow else { return }
            resetForNewSession(in: window)
        }
        .onChange(of: selectedPreset) { newPreset in
            if let preset = newPreset {
                loadDraftForPreset(preset)
            }
        }
        .onChange(of: draft) { _ in
            scheduleValidation()
        }
    }

    /// Whether an error diagnostic blocks launching/saving.
    private var hasBlockingError: Bool {
        diagnostics.contains(where: \.isError)
    }

    /// Re-validate the draft after a short debounce, so each keystroke does not
    /// spawn a resolver. Validation runs the server's resolve-preset over the
    /// current draft and keeps its diagnostics.
    private func scheduleValidation() {
        validationTask?.cancel()
        guard let preset = selectedPreset else {
            diagnostics = []
            return
        }
        let executablePath = preset.coreExecutablePath
        let current = draft
        validationTask = Task { @MainActor in
            try? await Task.sleep(nanoseconds: 300_000_000)
            if Task.isCancelled { return }
            let (_, diags) = await presetManager.resolveDraft(current, executablePath: executablePath)
            if Task.isCancelled { return }
            diagnostics = diags
        }
    }

    // MARK: - Preset Picker

    private var presetPickerSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Machine Preset")
                .font(.subheadline)
                .foregroundColor(.secondary)

            Picker("", selection: $selectedPreset) {
                if !presetManager.systemPresets.isEmpty {
                    Section("Built-in") {
                        ForEach(presetManager.systemPresets) { preset in
                            Text(preset.name).tag(Optional(preset))
                        }
                    }
                }
                if !presetManager.userPresets.isEmpty {
                    Section("My Presets") {
                        ForEach(presetManager.userPresets) { preset in
                            Text(preset.name).tag(Optional(preset))
                        }
                    }
                }
            }
            .pickerStyle(.menu)
            .labelsHidden()
            .disabled(presetManager.isDiscovering || isLaunching)
            .onChange(of: selectedPreset) { newValue in
                if let preset = newValue {
                    lastSelectedPresetId = preset.presetId
                }
            }
        }
    }

    // MARK: - Description

    private var descriptionSection: some View {
        VStack(alignment: .leading, spacing: 4) {
            if let preset = selectedPreset, let description = preset.modelDescription {
                Text(description)
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .frame(maxWidth: .infinity, alignment: .leading)
            } else if presetManager.isDiscovering {
                HStack(spacing: 8) {
                    ProgressView()
                        .scaleEffect(0.7)
                    Text("Discovering presets...")
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
            } else if presetManager.systemPresets.isEmpty {
                Text("No presets available")
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .italic()
            } else {
                Text("Select a preset to see its description")
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .italic()
            }
        }
        .frame(height: 40, alignment: .top)
    }

    // MARK: - Configuration Section

    /// The configuration editor. This dialog has one job -- configure a preset,
    /// then Create -- so the editor is always shown (the Welcome window handles
    /// launching an existing preset unaltered). While the schema loads, a
    /// spinner stands in its place.
    @ViewBuilder
    private var configurationSection: some View {
        if isLoadingSchema {
            HStack {
                ProgressView()
                    .scaleEffect(0.7)
                Text("Loading configuration...")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
            .frame(height: 200)
        } else {
            ConfigurationEditor(
                draft: $draft,
                originalDraft: originalDraft,
                storageSchema: storageSchema,
                memorySchema: memorySchema,
                hardDiscCapability: hardDiscCapability,
                extensionsSchema: extensionsSchema,
                econetSchema: econetSchema,
                startupSchema: startupSchema,
                romResolver: romResolver(for: selectedPreset),
                serialPorts: serialPorts,
                diagnostics: diagnostics
            )
            // Grow with the window (resizable) so the sideways list can
            // show more rows; 220 is the floor.
            .frame(minHeight: 220, maxHeight: .infinity)
        }
    }

    // MARK: - Save as Preset Section

    private var saveAsPresetSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            Toggle("Save as new preset", isOn: $saveAsNewPreset)
                .toggleStyle(.checkbox)

            if saveAsNewPreset {
                TextField("Preset name", text: $newPresetName)
                    .textFieldStyle(.roundedBorder)
            }
        }
    }

    // MARK: - Error Section

    @ViewBuilder
    private var errorSection: some View {
        if let error = launchError {
            ServerErrorView(message: error) { launchError = nil }
        }
    }

    // MARK: - Button Bar

    private var buttonBar: some View {
        HStack {
            Spacer()

            Button("Cancel") {
                dismiss()
            }
            .keyboardShortcut(.cancelAction)
            .disabled(isLaunching)

            Button(isLaunching ? "Creating..." : "Create") {
                Task {
                    await createMachine()
                }
            }
            .keyboardShortcut(.defaultAction)
            .disabled(selectedPreset == nil || isLaunching || presetManager.isDiscovering ||
                     hasBlockingError ||
                     (saveAsNewPreset && newPresetName.trimmingCharacters(in: .whitespaces).isEmpty))
        }
        .padding(16)
    }

    // MARK: - Actions

    /// Reset everything except the selected preset so that re-opening
    /// the dialog feels fresh. Called from the AppKit
    /// willCloseNotification handler so it covers every dismiss path
    /// (Cancel, Create-success, red close button, Cmd-W) without us
    /// having to instrument each one.
    ///
    /// The window is also resized back to the natural content height
    /// so AppKit doesn't reopen at whatever size the user had stretched to.
    private func resetForNewSession(in window: NSWindow) {
        saveAsNewPreset = false
        newPresetName = ""
        isLaunching = false
        launchError = nil
        validationTask?.cancel()
        diagnostics = []
        // Re-resolve selectedPreset against the current preset list.
        // createPreset() calls discoverPresets() which rebuilds the
        // systemPresets/userPresets arrays with fresh MachinePreset
        // structs - the existing selectedPreset reference goes stale
        // and the Picker can no longer match it, so the dropdown
        // appears empty on next open. restoreLastSelection() looks
        // up the preset by id (from AppStorage) and re-sets
        // selectedPreset to a live struct in the current list.
        restoreLastSelection()
        // Configs revert to the preset's defaults the next time
        // loadDraftForPreset runs (triggered by .onChange of
        // selectedPreset on next open if the value differs, or
        // explicitly in .task on the first open of the app session).
        // Force it here too, so even within a single app session a
        // close-then-open clears any edits.
        if let preset = selectedPreset {
            loadDraftForPreset(preset)
        }
        // AppKit remembers the window's frame across hide/show. Resize
        // to the natural content height so the next open isn't stuck at
        // whatever size the user had stretched to. The user can still
        // resize manually after opening.
        let width = window.frame.size.width
        // setContentSize takes the content rect; the actual frame
        // includes title bar etc. Use setContentSize for accuracy.
        window.setContentSize(NSSize(width: width, height: contentHeight))
    }

    private func restoreLastSelection() {
        let allPresets = presetManager.systemPresets + presetManager.userPresets

        // Try to restore last selection
        if !lastSelectedPresetId.isEmpty,
           let preset = allPresets.first(where: { $0.presetId == lastSelectedPresetId }) {
            selectedPreset = preset
        } else if let first = presetManager.systemPresets.first {
            // Default to first system preset
            selectedPreset = first
        }
    }

    /// Load the draft for a preset and the schemas that describe what its model
    /// can hold. The draft is the preset document as written (so the dialog
    /// edits exactly what will be launched); the schemas drive the editors'
    /// capabilities (FDC options, socket layout, drive count).
    private func loadDraftForPreset(_ preset: MachinePreset) {
        isLoadingSchema = true
        storageSchema = nil
        memorySchema = nil

        Task {
            let manager = presetManager
            let storage = await manager.fetchStorageSchema(for: preset)
            let sideways = await manager.fetchSidewaysSchema(for: preset)
            let extensions = await manager.fetchExtensionsSchema(for: preset)
            let econet = await manager.fetchEconetSchema(for: preset)
            let startup = await manager.fetchStartupSchema(for: preset)
            let ports = await manager.serialPortSuggestions()
            // The resolved document is the editing source: defaults filled,
            // paths absolute, default ROMs expanded into explicit slots. The
            // raw-file decode is a defensive fallback for the rare case the
            // resolver cannot be run.
            let (resolved, _) = await manager.resolvePreset(preset)
            var loaded = resolved ?? Self.loadDraft(fromFile: preset.presetFilepath) ?? MachineDraft()

            await MainActor.run {
                storageSchema = storage
                memorySchema = sideways
                extensionsSchema = extensions
                econetSchema = econet
                startupSchema = startup
                serialPorts = ports
                hardDiscCapability = extensions.flatMap { HardDiscCapability(schema: $0) }

                // Fill the metadata the launch needs if it was omitted.
                if loaded.name == nil { loaded.name = preset.name }
                if loaded.machineName == nil { loaded.machineName = preset.machineName }

                // Ensure a drive row per drive the model offers (resolve-preset
                // already lists both; this also covers the raw-file fallback).
                let driveCount = storage?.floppyDrives?.count ?? 2
                loaded.storage = Self.ensuringFloppyDrives(loaded.storage, count: driveCount)

                draft = loaded
                originalDraft = loaded
                isLoadingSchema = false
            }
        }
    }

    /// Decode the draft from a preset file, or nil if it cannot be read/parsed.
    /// A defensive fallback when `resolve-preset` cannot be run.
    private static func loadDraft(fromFile filepath: String) -> MachineDraft? {
        guard let data = FileManager.default.contents(atPath: filepath) else { return nil }
        return try? MachineDraft.decode(from: data)
    }

    /// A storage section with a drive row for each of `count` drives, preserving
    /// any drives the preset already configures.
    private static func ensuringFloppyDrives(_ storage: MachineDraft.Storage?, count: Int) -> MachineDraft.Storage {
        var result = storage ?? MachineDraft.Storage()
        var drives = result.floppyDrives ?? []
        let present = Set(drives.map(\.drive))
        for drive in 0..<count where !present.contains(drive) {
            drives.append(MachineDraft.FloppyDrive(drive: drive))
        }
        result.floppyDrives = drives.sorted { $0.drive < $1.drive }
        return result
    }

    /// A ROM-title resolver bound to the preset's core executable, for the
    /// Memory tab; nil before a preset is selected.
    private func romResolver(for preset: MachinePreset?) -> (@MainActor (String) async -> RomHeaderInfo?)? {
        guard let preset else { return nil }
        let manager = presetManager
        let executablePath = preset.coreExecutablePath
        return { image in
            await manager.fetchRomHeader(image: image, executablePath: executablePath)
        }
    }

    private func createMachine() async {
        guard let preset = selectedPreset else { return }

        isLaunching = true
        launchError = nil

        // Capture the manager reference before async call to avoid
        // @StateObject wrapper issues
        let manager = presetManager

        // If "Save as new preset" is on with a non-empty name, write the whole
        // draft - storage, floppies, sideways and all - into a new user preset
        // before launching, so selecting it later launches the same machine. The
        // launched machine is identical either way (the same draft document is
        // launched); the new preset simply makes the configuration discoverable
        // in the Machine Preset picker next time.
        let trimmedName = newPresetName.trimmingCharacters(
            in: .whitespacesAndNewlines)
        if saveAsNewPreset && !trimmedName.isEmpty {
            let (createdId, errorMessage) = await manager.saveDraftAsPreset(
                draft, name: trimmedName, executablePath: preset.coreExecutablePath)
            if createdId == nil {
                // Surface the real stderr from the import-preset subcommand
                // so the user can act on it. Don't guess - the actual failure
                // mode varies (name conflict, validation error, write
                // permissions on the user-presets directory, ...).
                let detail = (errorMessage?.isEmpty == false)
                    ? errorMessage!
                    : "The import-preset subcommand failed with no error output."
                launchError = "Couldn't save preset \"\(trimmedName)\":\n\(detail)"
                isLaunching = false
                return
            }
        }

        let result = await manager.launchCore(draft: draft, executablePath: preset.coreExecutablePath)

        switch result {
        case .success(let core):
            MachineManager.shared.register(
                process: core.process,
                port: core.port,
                provenanceUUID: core.provenanceUUID
            )
            let target = ConnectionTarget(host: "127.0.0.1", port: core.port)
            windowState.pendingTarget = target
            windowState.pendingNeedsRun = true
            windowState.pendingProvenanceUUID = core.provenanceUUID
            openWindow(id: "main")
            // Reset transient state before dismissing. SwiftUI keeps the
            // View (and its @State) alive across the singleton window's
            // hide/show cycle, so without this the next File > New... shows
            // the dialog still wedged in "Creating..." with controls
            // disabled. The failure branch already resets isLaunching;
            // mirror it here so success doesn't leak the in-flight state.
            isLaunching = false
            launchError = nil
            dismiss()

        case .failure(let error):
            launchError = error.localizedDescription
            isLaunching = false
        }
    }
}
