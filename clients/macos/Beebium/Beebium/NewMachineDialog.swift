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
    @State private var showConfiguration = false
    @StateObject private var storageConfig = StorageConfigurationState()
    @State private var storageSchema: StorageSchemaSection?
    @StateObject private var memoryConfig = MemoryConfigurationState()
    @State private var memorySchema: SidewaysSchemaSection?
    @State private var isLoadingSchema = false

    // Save as preset
    @State private var saveAsNewPreset = false
    @State private var newPresetName = ""

    // Launch state
    @State private var isLaunching = false
    @State private var launchError: String?

    // The hosting NSWindow, captured via WindowAccessor so we can resize
    // it programmatically when the Configuration disclosure toggles. The
    // window stays user-resizable; we just adjust the height to fit each
    // mode so a previously-expanded window doesn't leave a sea of empty
    // space after the user collapses Configuration.
    @State private var hostingWindow: NSWindow?

    private var dialogWidth: CGFloat {
        showConfiguration ? 520 : 380
    }

    // Empirical natural content heights for the two modes. Tuned so the
    // dialog fits cleanly with no scrollbar and no large gap below the
    // button bar.
    private let collapsedContentHeight: CGFloat = 200
    private let expandedContentHeight: CGFloat = 560

    var body: some View {
        VStack(spacing: 0) {
            // Content
            VStack(alignment: .leading, spacing: 16) {
                presetPickerSection
                descriptionSection
                configurationSection
                if showConfiguration {
                    saveAsPresetSection
                }
                errorSection
            }
            .padding(20)

            Divider()

            // Buttons
            buttonBar
        }
        .frame(minWidth: dialogWidth, maxWidth: .infinity)
        .animation(.easeInOut(duration: 0.2), value: showConfiguration)
        .background(WindowAccessor(window: $hostingWindow))
        .onChange(of: showConfiguration) { isExpanded in
            resizeWindowForMode(expanded: isExpanded)
        }
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
                loadSchemaForPreset(preset)
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
                loadSchemaForPreset(preset)
            }
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

    private var configurationSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            // Disclosure button
            Button {
                withAnimation {
                    showConfiguration.toggle()
                }
            } label: {
                HStack(spacing: 4) {
                    Image(systemName: showConfiguration ? "chevron.down" : "chevron.right")
                        .font(.caption)
                        .frame(width: 12)
                    Text("Configuration")
                        .font(.subheadline)
                }
                .foregroundColor(.primary)
            }
            .buttonStyle(.plain)
            .disabled(selectedPreset == nil)

            // Configuration editor (shown when expanded)
            if showConfiguration {
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
                        storageConfig: storageConfig,
                        storageSchema: storageSchema,
                        memoryConfig: memoryConfig,
                        memorySchema: memorySchema,
                        modelId: selectedPreset?.modelName ?? "model-b"
                    )
                    // Grow with the window (resizable) so the sideways list can
                    // show more rows; 220 is the floor.
                    .frame(minHeight: 220, maxHeight: .infinity)
                }
            }
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
            HStack(alignment: .top, spacing: 10) {
                Image(systemName: "exclamationmark.triangle.fill")
                    .foregroundColor(.red)
                    .imageScale(.large)
                Text(error)
                    .font(.callout)
                    .foregroundColor(.primary)
                    .fixedSize(horizontal: false, vertical: true)
                    .textSelection(.enabled)
                Spacer(minLength: 0)
            }
            .padding(10)
            .background(Color.red.opacity(0.08))
            .overlay(
                RoundedRectangle(cornerRadius: 6)
                    .stroke(Color.red.opacity(0.25), lineWidth: 1)
            )
            .cornerRadius(6)
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
    /// The window is also resized back to the collapsed content height
    /// so AppKit remembers a small frame and the next open doesn't
    /// reappear at the previously-expanded size.
    private func resetForNewSession(in window: NSWindow) {
        showConfiguration = false
        saveAsNewPreset = false
        newPresetName = ""
        isLaunching = false
        launchError = nil
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
        // loadSchemaForPreset runs (triggered by .onChange of
        // selectedPreset on next open if the value differs, or
        // explicitly in .task on the first open of the app session).
        // Force it here too, so even within a single app session a
        // close-then-open clears any edits.
        if let preset = selectedPreset {
            loadSchemaForPreset(preset)
        }
        // AppKit remembers the window's frame across hide/show. Resize
        // to the collapsed content height so the next open isn't
        // stuck at whatever size the user had stretched to. The user
        // can still resize manually after opening.
        let width = window.frame.size.width
        // setContentSize takes the content rect; the actual frame
        // includes title bar etc. Use setContentSize for accuracy.
        window.setContentSize(NSSize(width: width, height: collapsedContentHeight))
    }

    /// Resize the hosting window when the Configuration disclosure toggles.
    /// Collapse: shrink to the natural collapsed height regardless of
    /// whatever larger size the user had stretched to - the collapsed
    /// dialog has nothing to fill the extra space and looks broken.
    /// Expand: grow to the expanded height only if the window is
    /// currently smaller than that. If the user has already manually
    /// stretched it taller, leave their choice alone.
    private func resizeWindowForMode(expanded: Bool) {
        guard let window = hostingWindow else { return }
        let currentSize = window.contentLayoutRect.size
        let targetHeight = expanded ? expandedContentHeight : collapsedContentHeight
        let newHeight = expanded
            ? max(currentSize.height, targetHeight)
            : targetHeight
        if abs(newHeight - currentSize.height) < 0.5 { return }
        window.setContentSize(NSSize(width: currentSize.width, height: newHeight))
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

    private func loadSchemaForPreset(_ preset: MachinePreset) {
        isLoadingSchema = true
        storageSchema = nil
        memorySchema = nil

        // Reset storage config to defaults
        storageConfig.fdcSocketId = "none"
        storageConfig.clearAllDrives()

        Task {
            let manager = presetManager
            let executablePath = preset.coreExecutablePath
            let schema = await manager.fetchStorageSchema(for: preset)
            let sidewaysSchema = await manager.fetchSidewaysSchema(for: preset)
            let presetSlots = manager.sidewaysSlots(for: preset)

            await MainActor.run {
                storageSchema = schema

                // Update drive count based on schema
                let driveCount = schema?.floppyDrives?.count ?? 2
                storageConfig.drives = (0..<driveCount).map {
                    StorageConfigurationState.DriveConfig(id: $0, imageFilepath: nil)
                }

                // Build the Memory tab from the machine's sockets and the
                // preset's own sideways assignments.
                memorySchema = sidewaysSchema
                if let sidewaysSchema = sidewaysSchema {
                    memoryConfig.configure(schema: sidewaysSchema, presetSlots: presetSlots)
                    memoryConfig.headerResolver = { @MainActor image in
                        await manager.fetchRomHeader(image: image, executablePath: executablePath)
                    }
                } else {
                    memoryConfig.sockets = []
                    memoryConfig.headerResolver = nil
                }

                isLoadingSchema = false
            }

            // Resolve ROM titles (one describe-rom per loaded image) so the
            // Memory tab shows real ROM names rather than image filenames.
            await memoryConfig.resolveTitles()
        }
    }

    private func createMachine() async {
        guard let preset = selectedPreset else { return }

        isLaunching = true
        launchError = nil

        // Capture the manager reference before async call to avoid
        // @StateObject wrapper issues
        let manager = presetManager

        // If "Save as new preset" is on with a non-empty name, snapshot
        // the user's sideways configuration into a new user preset
        // before launching. The launched machine is identical either
        // way (same CLI args yield the same machine); the new preset
        // simply makes the chosen configuration discoverable in the
        // Machine Preset picker next time.
        let trimmedName = newPresetName.trimmingCharacters(
            in: .whitespacesAndNewlines)
        if saveAsNewPreset && !trimmedName.isEmpty {
            let (createdId, errorMessage) = await manager.createPreset(
                basedOn: preset,
                newName: trimmedName,
                sidewaysArguments: memoryConfig.sidewaysLaunchArguments()
            )
            if createdId == nil {
                // Surface the real stderr from the create-preset subcommand
                // so the user can act on it. Don't guess - the actual failure
                // mode varies (name conflict, validation error, write
                // permissions on the user-presets directory, ...).
                let detail = (errorMessage?.isEmpty == false)
                    ? errorMessage!
                    : "The create-preset subcommand failed with no error output."
                launchError = "Couldn't save preset \"\(trimmedName)\":\n\(detail)"
                isLaunching = false
                return
            }
        }

        let result = await manager.launchCore(preset, storageConfig: storageConfig, memoryConfig: memoryConfig)

        switch result {
        case .success(let core):
            MachineManager.shared.register(
                process: core.process,
                port: core.port,
                provenanceUUID: core.provenanceUUID,
                name: core.machineName
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
