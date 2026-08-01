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

import AppKit
import SwiftUI

/// Header showing mode icon and title at top of sidebar content
private struct SidebarHeader: View {
    let mode: SidebarMode

    var body: some View {
        HStack(spacing: 8) {
            Image(systemName: mode.icon)
                .font(.system(size: 18))
                .foregroundColor(.secondary)
            Text(mode.label)
                .font(.headline)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.horizontal, 16)
        .padding(.vertical, 12)
    }
}

/// Container view that displays content for the selected sidebar mode
struct SidebarModeContent: View {
    let mode: SidebarMode
    @ObservedObject var discClient: DiscClient
    @ObservedObject var indicatorClient: IndicatorClient
    @ObservedObject var keyboardMappingManager: KeyboardMappingManager
    @ObservedObject var audioClient: AudioClient
    @ObservedObject var audioMixerState: AudioMixerState
    @ObservedObject var econetClient: EconetClient
    @ObservedObject var serialClient: SerialClient
    @ObservedObject var extensionUiClient: ExtensionUiClient
    @ObservedObject var peripheralsClient: PeripheralsClient
    @ObservedObject var transportsClient: EconetTransportsClient
    @ObservedObject var sidewaysClient: SidewaysClient
    @ObservedObject var systemClient: SystemClient
    @ObservedObject var videoSettings: VideoSettings
    @ObservedObject var speedModel: SpeedControlModel

    var body: some View {
        VStack(spacing: 0) {
            SidebarHeader(mode: mode)

            switch mode {
            case .storage:
                StorageModeView(discClient: discClient,
                                peripheralsClient: peripheralsClient,
                                indicatorClient: indicatorClient,
                                isServerLocal: systemClient.isServerLocal)
            case .memory:
                MemoryModeView(sidewaysClient: sidewaysClient,
                               isServerLocal: systemClient.isServerLocal)
            case .peripherals:
                PeripheralsModeView(client: peripheralsClient,
                                    extensionUiClient: extensionUiClient,
                                    serialClient: serialClient)
            case .video:
                VideoModeView(videoSettings: videoSettings)
            case .sound:
                AudioMixerView(audioClient: audioClient, mixerState: audioMixerState)
            case .keyboard:
                KeyboardModeView(mappingManager: keyboardMappingManager)
            case .processor:
                ProcessorModeView(speedModel: speedModel)
            case .network:
                NetworkModeView(econetClient: econetClient,
                                keyboardMappingManager: keyboardMappingManager,
                                extensionUiClient: extensionUiClient,
                                transportsClient: transportsClient)
            }
        }
    }
}

// MARK: - Placeholder Mode Views

/// Video mode: pick a display style and tweak its options.
struct VideoModeView: View {
    @ObservedObject var videoSettings: VideoSettings

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                pixelShapeSection
                Divider()
                stylePickerSection
                if videoSettings.activeStyle.hasOptions {
                    Divider()
                    styleOptionsSection
                }
                Divider()
                backgroundColourSection
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 12)
        }
    }

    // MARK: - Background colour

    private var backgroundColourSection: some View {
        HStack {
            Text("Boxing Colour")
            Spacer()
            ResettableColorPicker(
                accessibilityLabel: "Boxing colour",
                color: $videoSettings.windowBackground,
                defaultColor: VideoSettings.defaultWindowBackground
            )
        }
    }

    // MARK: - Pixels (PAR)

    private var pixelShapeSection: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Pixels")
                .font(.subheadline)
                .foregroundColor(.secondary)
            Picker("", selection: $videoSettings.pixelShape) {
                ForEach(PixelShape.allCases) { shape in
                    Text(shape.displayName).tag(shape)
                }
            }
            .labelsHidden()
            .pickerStyle(.segmented)
        }
    }

    // MARK: - Style Picker

    private var stylePickerSection: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Display Style")
                .font(.subheadline)
                .foregroundColor(.secondary)
            // Bind through a derived binding so changes go through
            // selectStyle(id:) (rejects unknown ids).
            Picker("", selection: stylePickerBinding) {
                ForEach(videoSettings.availableStyles, id: \.id) { style in
                    Text(style.displayName).tag(style.id)
                }
            }
            .labelsHidden()
            .pickerStyle(.segmented)
        }
    }

    private var stylePickerBinding: Binding<String> {
        Binding(
            get: { videoSettings.activeStyleID },
            set: { videoSettings.selectStyle(id: $0) }
        )
    }

    // MARK: - Per-style Options

    private var styleOptionsSection: some View {
        // .id() forces the view to rebuild when the active style changes, so
        // styles with @Published parameters bind to the right instance.
        videoSettings.activeStyle.makeOptionsView()
            .id(videoSettings.activeStyleID)
    }
}

// SoundModeView replaced by AudioMixerView

/// Keyboard mode view showing mapping selection
struct KeyboardModeView: View {
    @ObservedObject var mappingManager: KeyboardMappingManager

    private var builtInMappings: [KeyboardMapping] {
        mappingManager.mappings.filter { $0.isBuiltIn }
    }

    private var userMappings: [KeyboardMapping] {
        mappingManager.mappings.filter { !$0.isBuiltIn }
    }

    /// Binding for Caps Lock sync toggle that sets the session override
    private var capsLockSyncBinding: Binding<Bool> {
        Binding(
            get: { mappingManager.isCapsLockSyncEnabled },
            set: { mappingManager.capsLockSyncOverride = $0 }
        )
    }

    /// Binding for a specific key's disabled state
    private func disabledKeyBinding(for keyName: String) -> Binding<Bool> {
        Binding(
            get: {
                mappingManager.isKeyDisabled(keyName)
            },
            set: { newValue in
                // Initialize override dictionary if needed
                if mappingManager.disabledKeysOverride == nil {
                    mappingManager.disabledKeysOverride = [:]
                }
                mappingManager.disabledKeysOverride?[keyName] = newValue
            }
        )
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            List {
                Section {
                    ForEach(builtInMappings) { mapping in
                        MappingRowView(
                            mapping: mapping,
                            isActive: mapping.id == mappingManager.activeMapping?.id,
                            onSelect: { selectMapping(mapping) }
                        )
                    }
                } header: {
                    Text("Beebium")
                }

                if !userMappings.isEmpty {
                    Section {
                        ForEach(userMappings) { mapping in
                            MappingRowView(
                                mapping: mapping,
                                isActive: mapping.id == mappingManager.activeMapping?.id,
                                onSelect: { selectMapping(mapping) }
                            )
                            .contextMenu {
                                if let url = mappingManager.fileURL(for: mapping) {
                                    Button {
                                        NSWorkspace.shared.activateFileViewerSelecting([url])
                                    } label: {
                                        Label("Reveal in Finder", systemImage: "folder")
                                    }
                                }
                            }
                        }
                    } header: {
                        Text("Custom")
                    }
                }
            }
            .listStyle(.sidebar)

            Divider()

            // Mapping reference table (only if entries with actions exist)
            if !mappingManager.mappingReferenceEntries.isEmpty {
                VStack(alignment: .leading, spacing: 4) {
                    ForEach(mappingManager.mappingReferenceEntries) { entry in
                        HStack(spacing: 0) {
                            // Host key column
                            Text(entry.hostKeyLabel)
                                .font(.system(.body).weight(.medium))
                                .foregroundColor(.primary)
                                .frame(width: 80, alignment: .leading)

                            // Function column
                            Text(entry.action)
                                .font(.body)
                                .foregroundColor(.secondary)
                                .lineLimit(1)

                            Spacer()
                        }
                    }
                }
                .padding(.horizontal, 16)
                .padding(.vertical, 10)

                Divider()
            }

            // Disableable keys section
            if !mappingManager.disableableKeyNames.isEmpty {
                ForEach(mappingManager.disableableKeyNames, id: \.self) { keyName in
                    Toggle("Disable \(keyName)", isOn: disabledKeyBinding(for: keyName))
                        .toggleStyle(.checkbox)
                        .padding(.horizontal, 16)
                        .padding(.vertical, 10)
                }

                Divider()
            }

            Toggle("Synchronize Caps Lock", isOn: capsLockSyncBinding)
                .toggleStyle(.checkbox)
                .padding(.horizontal, 16)
                .padding(.vertical, 10)
        }
    }

    private func selectMapping(_ mapping: KeyboardMapping) {
        mappingManager.activeMapping = mapping
    }
}

/// Row view for a single keyboard mapping
private struct MappingRowView: View {
    let mapping: KeyboardMapping
    let isActive: Bool
    let onSelect: () -> Void

    var body: some View {
        Button(action: onSelect) {
            HStack {
                Text(mapping.name)
                    .lineLimit(1)
                Spacer()
                if isActive {
                    Image(systemName: "checkmark")
                        .foregroundColor(.accentColor)
                        .font(.system(size: 12, weight: .semibold))
                }
            }
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }
}

/// Processor mode: the host CPU (emulation speed) and, in future, Tube
/// coprocessors.
struct ProcessorModeView: View {
    @ObservedObject var speedModel: SpeedControlModel

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 20) {
                SpeedControlView(model: speedModel)
                // Future: Tube coprocessor selection/config.
            }
            .padding()
        }
        .onAppear { speedModel.startPolling() }
        .onDisappear { speedModel.stopPolling() }
    }
}

/// Network mode view showing Econet status, connection controls, and peer list
struct NetworkModeView: View {
    @ObservedObject var econetClient: EconetClient
    @ObservedObject var keyboardMappingManager: KeyboardMappingManager
    @ObservedObject var extensionUiClient: ExtensionUiClient
    @ObservedObject var transportsClient: EconetTransportsClient
    @State private var showStationIdPopover = false

    var body: some View {
        if !econetClient.isLoaded {
            loadingView
        } else if !econetClient.enabled {
            notFittedView
        } else {
            econetContentView
        }
    }

    // MARK: - Loading State

    private var loadingView: some View {
        VStack(spacing: 12) {
            if let error = econetClient.errorMessage {
                Image(systemName: "exclamationmark.triangle")
                    .font(.system(size: 24))
                    .foregroundColor(.yellow)
                Text("Connection Error")
                    .font(.headline)
                    .foregroundColor(.secondary)
                Text(error)
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .multilineTextAlignment(.center)
            } else {
                ProgressView()
                Text("Loading...")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    // MARK: - Not Fitted State

    private var notFittedView: some View {
        VStack(spacing: 12) {
            Image(systemName: "network")
                .font(.system(size: 32))
                .foregroundColor(.secondary)
            Text("Econet Not Fitted")
                .font(.headline)
                .foregroundColor(.secondary)
            Text("Econet hardware is not fitted in this machine configuration.")
                .font(.caption)
                .foregroundColor(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 16)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    // MARK: - Enabled Content

    private var econetContentView: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                if econetClient.gatedBySpeed {
                    speedGatedBanner
                        .padding(.horizontal, 16)
                        .padding(.top, 12)
                }

                statusSection
                    .padding(.horizontal, 16)
                    .padding(.vertical, 12)

                Divider()
                    .padding(.horizontal, 12)

                // Per-transport panels driven by ExtensionUiService.
                // The active transports are discovered at runtime via
                // EconetTransportService.ListTransports; for each one
                // that implements an Extension UI we render its panel,
                // keyed by the server-assigned instance id. The client
                // holds no knowledge of specific transport types, so a
                // new transport extension surfaces here automatically.
                ForEach(transportsClient.transports.filter(\.hasUI)) { transport in
                    ExtensionPanelView(client: extensionUiClient,
                                       extensionID: transport.id)
                        .padding(.horizontal, 16)
                        .padding(.vertical, 12)
                }
            }
        }
        .task { await transportsClient.refresh() }
    }

    // MARK: - Speed Gating Banner

    // Shown when the active transport requires real-time emulation (e.g.
    // Piconet) but the emulation speed is not 1x, so its traffic is severed.
    private var speedGatedBanner: some View {
        HStack(alignment: .top, spacing: 8) {
            Image(systemName: "exclamationmark.triangle.fill")
                .foregroundColor(.yellow)
            VStack(alignment: .leading, spacing: 2) {
                Text("Econet paused")
                    .font(.callout)
                    .bold()
                Text("This network bridges to real hardware and needs real-time speed. Set the emulation speed to 1x in the Processor panel to resume.")
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
            Spacer(minLength: 0)
        }
        .padding(10)
        .background(Color.yellow.opacity(0.12))
        .overlay(
            RoundedRectangle(cornerRadius: 8)
                .stroke(Color.yellow.opacity(0.4), lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 8))
    }

    // MARK: - Status Section

    private var statusSection: some View {
        VStack(alignment: .leading, spacing: 12) {
            // Transport-agnostic connection state. Driven by
            // EconetService.GetEconetStatus.connected which reflects
            // whatever the active transport's backend reports
            // (AunBackend's cable simulation for AUN; the serial
            // port's is_open() for Piconet). The previous aunMode
            // gate that hid this state for non-AUN transports was
            // dishonest -- a Piconet backend with the device
            // unplugged is just as "disconnected" as an AUN backend
            // with the cable virtually unplugged. The action button
            // (Connect/Disconnect for AUN, future Reconnect for
            // Piconet) lives in the per-transport panel below the
            // header, where it belongs.
            HStack {
                Text("Connection")
                    .foregroundColor(.secondary)
                Spacer()
                Image(systemName: "circle.fill")
                    .font(.system(size: 8))
                    .foregroundColor(econetClient.connected ? .green : .secondary)
                Text(econetClient.connected ? "Connected" : "Disconnected")
                    .fontWeight(.medium)
            }

            HStack {
                Text("Econet Station")
                    .foregroundColor(.secondary)
                Spacer()
                Text("\(econetClient.stationId)")
                    .fontWeight(.medium)
                Button {
                    showStationIdPopover = true
                } label: {
                    Image(systemName: "square.and.pencil")
                }
                .buttonStyle(.borderless)
                .help("Edit Econet station")
                .popover(isPresented: $showStationIdPopover, arrowEdge: .trailing) {
                    StationIdPopover(
                        currentStationId: econetClient.stationId,
                        econetClient: econetClient,
                        breakKeyLabel: keyboardMappingManager.breakKeyLabel,
                        isPresented: $showStationIdPopover
                    )
                }
            }

            // The Connect/Disconnect button + AUN Port row lived
            // here previously. Both are AUN-specific concerns and
            // now live in the AUN panel below (rendered via
            // ExtensionPanelView). The transport-agnostic header
            // shows only what every Econet machine has: link state
            // and station number.
        }
    }

    // Peers Section deleted: AUN's peer list is now rendered by
    // ExtensionPanelView(extensionID: "aun") via AunUi (server-driven).
}

/// Popover for editing the Econet station ID
private struct StationIdPopover: View {
    let currentStationId: UInt32
    @ObservedObject var econetClient: EconetClient
    let breakKeyLabel: String?
    @Binding var isPresented: Bool
    @State private var stationIdText: String = ""
    @State private var isSaving: Bool = false
    @State private var validationError: String?

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Econet Station")
                .font(.headline)

            TextField("1\u{2013}254", text: $stationIdText)
                .textFieldStyle(.roundedBorder)
                .frame(width: 120)
                .onSubmit { save() }

            if let error = validationError {
                Text(error)
                    .font(.caption)
                    .foregroundColor(.red)
            }

            HStack(spacing: 4) {
                Image(systemName: "info.circle")
                    .font(.caption)
                if let label = breakKeyLabel {
                    Text("Takes effect on next Break (\(label)).")
                        .font(.caption)
                } else {
                    Text("Takes effect on next Break.")
                        .font(.caption)
                }
            }
            .foregroundColor(.secondary)

            HStack {
                Button("Cancel") {
                    isPresented = false
                }
                .keyboardShortcut(.cancelAction)
                Spacer()
                Button("Save") {
                    save()
                }
                .keyboardShortcut(.defaultAction)
                .disabled(isSaving)
            }
        }
        .padding(16)
        .frame(width: 240)
        .onAppear {
            stationIdText = "\(currentStationId)"
        }
    }

    private func save() {
        guard let value = UInt32(stationIdText),
              value >= 1, value <= 254 else {
            validationError = "Station number must be between 1 and 254."
            return
        }
        validationError = nil
        isSaving = true
        Task {
            let result = await econetClient.setStationId(value)
            isSaving = false
            switch result {
            case .success:
                isPresented = false
            case .failure(let error):
                validationError = error.localizedDescription
            }
        }
    }
}

// MARK: - Common Placeholder

/// Generic placeholder view for unimplemented modes
private struct ModePlaceholder: View {
    let mode: SidebarMode

    var body: some View {
        Text("Coming soon")
            .font(.subheadline)
            .foregroundColor(.secondary)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

#if DEBUG
struct SidebarModeContent_Previews: PreviewProvider {
    static var previews: some View {
        SidebarModeContent(
            mode: .keyboard,
            discClient: DiscClient(),
            indicatorClient: IndicatorClient(),
            keyboardMappingManager: KeyboardMappingManager(),
            audioClient: AudioClient(),
            audioMixerState: AudioMixerState(),
            econetClient: EconetClient(),
            serialClient: SerialClient(),
            extensionUiClient: ExtensionUiClient(),
            peripheralsClient: PeripheralsClient(),
            transportsClient: EconetTransportsClient(),
            sidewaysClient: SidewaysClient(),
            systemClient: SystemClient(),
            videoSettings: VideoSettings(),
            speedModel: SpeedControlModel()
        )
        .frame(width: 220, height: 300)
        .background(Color(nsColor: .windowBackgroundColor))
    }
}
#endif
