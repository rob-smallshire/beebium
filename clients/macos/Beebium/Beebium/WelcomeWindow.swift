// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

/// Welcome window shown on bare app launch. Displays preset cards for one-click
/// machine creation and a "New Machine..." button for custom configuration.
struct WelcomeWindowContent: View {
    var onDismiss: (() -> Void)?
    @StateObject private var presetManager = PresetManager.shared
    @ObservedObject private var connectWindowState = ConnectWindowState.shared
    @Environment(\.openWindow) private var openWindow
    @State private var launchError: String?
    @State private var isLaunching = false
    @State private var gridHeight: CGFloat = 376

    private var appVersion: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? ""
    }

    var body: some View {
        VStack(spacing: 16) {
            // App icon and title
            VStack(spacing: 4) {
                if let appIcon = NSApp.applicationIconImage {
                    // Large enough that the icon's detailed representation is
                    // chosen: below 64px the artwork is only its palette, and
                    // the solids on the floor do not resolve at all.
                    Image(nsImage: appIcon)
                        .resizable()
                        .frame(width: 144, height: 144)
                        .accessibilityLabel("Beebium")
                }
                Text("Beebium")
                    .font(.title2)
                    .fontWeight(.semibold)
                if !appVersion.isEmpty {
                    Text("Version \(appVersion)")
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
            }
            .padding(.top, 16)

            // Preset grid (scrollable for many presets)
            if presetManager.isDiscovering {
                ProgressView("Discovering presets...")
                    .padding()
            } else if presetManager.systemPresets.isEmpty && presetManager.userPresets.isEmpty {
                Text("No presets found")
                    .foregroundColor(.secondary)
                    .padding()
            } else {
                let allPresets = presetManager.systemPresets + presetManager.userPresets
                ScrollView {
                    LazyVGrid(columns: [GridItem(.adaptive(minimum: 176, maximum: 176), spacing: 16)], spacing: 16) {
                        ForEach(allPresets) { preset in
                            PresetCard(preset: preset, isLaunching: isLaunching) {
                                Task { await launchPreset(preset) }
                            }
                        }
                    }
                    .padding(.horizontal, 24)
                    .background(GeometryReader { geo in
                        Color.clear.preference(key: GridHeightKey.self, value: geo.size.height)
                    })
                }
                .frame(minHeight: 100, idealHeight: gridHeight, maxHeight: gridHeight)
                .onPreferenceChange(GridHeightKey.self) { gridHeight = $0 }

                // Error display
                if let error = launchError {
                    Text(error)
                        .foregroundColor(.red)
                        .font(.caption)
                        .padding(.horizontal)
                }
            }

            // New Machine button
            HStack {
                Spacer()
                Button("New Machine...") {
                    connectWindowState.pendingTarget = nil
                    connectWindowState.pendingNeedsRun = false
                    connectWindowState.pendingProvenanceUUID = nil
                    openWindow(id: "new-machine")
                }
            }
            .padding(.horizontal, 24)
            .padding(.bottom, 16)
        }
        .frame(minWidth: 420, idealWidth: 600, maxWidth: .infinity)
        .onReceive(NotificationCenter.default.publisher(for: .didOpenEmulatorWindow)) { _ in
            onDismiss?()
        }
        .task {
            // Capture openWindow into shared AppActions for FileCommands
            AppActions.shared.openNewMachine = { [openWindow] in openWindow(id: "new-machine") }
            AppActions.shared.openConnect = { [openWindow] in openWindow(id: "connect") }
            AppActions.shared.openWelcome = { [openWindow] in
                ConnectWindowState.shared.pendingTarget = nil
                ConnectWindowState.shared.pendingNeedsRun = false
                ConnectWindowState.shared.pendingProvenanceUUID = nil
                openWindow(id: "main")
            }
            if presetManager.systemPresets.isEmpty && !presetManager.isDiscovering {
                await presetManager.discoverPresets()
            }
        }
    }

    private func launchPreset(_ preset: MachinePreset) async {
        isLaunching = true
        launchError = nil

        let manager = PresetManager.shared
        let result = await manager.launchCore(preset)

        switch result {
        case .success(let core):
            MachineManager.shared.register(
                process: core.process,
                port: core.port,
                provenanceUUID: core.provenanceUUID
            )
            let target = ConnectionTarget(host: "127.0.0.1", port: core.port)
            connectWindowState.pendingTarget = target
            connectWindowState.pendingNeedsRun = true
            connectWindowState.pendingProvenanceUUID = core.provenanceUUID
            openWindow(id: "main")
            isLaunching = false

        case .failure(let error):
            launchError = error.localizedDescription
            isLaunching = false
        }
    }
}

/// Preference key for measuring the grid's natural height.
private struct GridHeightKey: PreferenceKey {
    static var defaultValue: CGFloat = 376
    static func reduce(value: inout CGFloat, nextValue: () -> CGFloat) {
        value = max(value, nextValue())
    }
}

/// A clickable card representing a machine preset with thumbnail and name.
struct PresetCard: View {
    let preset: MachinePreset
    let isLaunching: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            VStack(spacing: 0) {
                // Thumbnail area
                thumbnailView
                    .frame(width: 160, height: 120)
                    .clipped()

                // Label area — fixed height so all cards are uniform
                VStack(spacing: 2) {
                    Text(preset.name)
                        .font(.callout)
                        .fontWeight(.medium)
                        .lineLimit(2)
                        .multilineTextAlignment(.center)
                    Text(preset.modelDescription ?? " ")
                        .font(.caption)
                        .foregroundColor(.secondary)
                        .lineLimit(2)
                        .multilineTextAlignment(.center)
                }
                .frame(width: 144, height: 48)
                .padding(.vertical, 6)
                .padding(.horizontal, 8)
            }
            .background(Color(nsColor: .controlBackgroundColor))
            .cornerRadius(8)
            .overlay(
                RoundedRectangle(cornerRadius: 8)
                    .stroke(Color(nsColor: .separatorColor), lineWidth: 1)
            )
        }
        .buttonStyle(.plain)
        .disabled(isLaunching)
    }

    @ViewBuilder
    private var thumbnailView: some View {
        if let thumbnailURL = thumbnailURL(for: preset),
           let nsImage = NSImage(contentsOf: thumbnailURL) {
            ZStack {
                Color.black
                Image(nsImage: nsImage)
                    .resizable()
                    .interpolation(.none)
                    .aspectRatio(contentMode: .fit)
            }
        } else {
            ZStack {
                Color.black
                Image(systemName: "desktopcomputer")
                    .font(.system(size: 32))
                    .foregroundColor(.gray)
            }
        }
    }

    /// Look for a sidecar thumbnail PNG alongside the preset file.
    /// Convention: model-b.preset.beebium -> model-b.thumbnail.png
    private func thumbnailURL(for preset: MachinePreset) -> URL? {
        let presetURL = URL(fileURLWithPath: preset.presetFilepath)
        // Strip .preset.beebium extension to get the base name
        var basename = presetURL.deletingPathExtension().lastPathComponent  // strips .beebium
        if basename.hasSuffix(".preset") {
            basename = String(basename.dropLast(".preset".count))
        }
        let thumbnailURL = presetURL.deletingLastPathComponent()
            .appendingPathComponent("\(basename).thumbnail.png")
        return FileManager.default.fileExists(atPath: thumbnailURL.path) ? thumbnailURL : nil
    }
}
