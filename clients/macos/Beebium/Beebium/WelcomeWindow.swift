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
    @StateObject private var savedMachines = SavedMachinesModel()
    @ObservedObject private var connectWindowState = ConnectWindowState.shared
    @Environment(\.openWindow) private var openWindow
    @State private var page: WelcomePage = .presets
    @State private var launchError: String?
    @State private var launchErrorPresetName: String?
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

            // Saved machines to reopen, or the presets and the custom builder.
            Picker("", selection: $page) {
                Text("Machines").tag(WelcomePage.machines)
                Text("Presets").tag(WelcomePage.presets)
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            .fixedSize()

            if page == .machines {
                MachinesPage(model: savedMachines, gridHeight: gridHeight)
            } else {
                presetsContent
            }
        }
        .frame(minWidth: 420, idealWidth: 600, maxWidth: .infinity)
        .onReceive(NotificationCenter.default.publisher(for: .didOpenEmulatorWindow)) { _ in
            onDismiss?()
        }
        .onReceive(NotificationCenter.default.publisher(for: .welcomePageRequested)) { note in
            // A File menu command (New -> presets, Open Recent -> machines) asking
            // an already-open Welcome window to switch pages.
            if let requested = note.object as? WelcomePage { page = requested }
        }
        .onAppear {
            // A pending page from the command that opened this window (New or Open
            // Recent); otherwise the launch default -- a returning user with saved
            // machines lands on Machines, a new user on Presets. Set here (not as
            // the @State default) so reading the recents list stays on the main
            // actor.
            page = AppActions.shared.pendingWelcomePage
                ?? WelcomePage.initial(hasRecentMachines: savedMachines.hasRecentMachines)
            AppActions.shared.pendingWelcomePage = nil
        }
        .task {
            // Capture openWindow into shared AppActions for FileCommands
            AppActions.shared.openConnect = { [openWindow] in openWindow(id: "connect") }
            AppActions.shared.showWelcome = { [openWindow] page in
                if WelcomeWindowRegistry.shared.focusExisting() {
                    if let page {
                        NotificationCenter.default.post(name: .welcomePageRequested, object: page)
                    }
                    return
                }
                ConnectWindowState.shared.pendingTarget = nil
                ConnectWindowState.shared.pendingNeedsRun = false
                ConnectWindowState.shared.pendingProvenanceUUID = nil
                AppActions.shared.pendingWelcomePage = page
                openWindow(id: "main")
            }
            if presetManager.systemPresets.isEmpty && !presetManager.isDiscovering {
                await presetManager.discoverPresets()
            }
        }
    }

    /// The presets page: the grid of system and user presets plus the custom
    /// builder. This is what the Welcome window showed before it gained the
    /// Machines page.
    @ViewBuilder
    private var presetsContent: some View {
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
                        PresetCard(preset: preset, isLaunching: isLaunching,
                                   thumbnailCache: presetManager.thumbnailCache) {
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
                ServerErrorView(
                    message: error,
                    title: launchErrorPresetName.map { "Couldn't start \"\($0)\"" }
                ) {
                    launchError = nil
                    launchErrorPresetName = nil
                }
                .padding(.horizontal, 24)
            }
        }

        WelcomeBottomBar {
            Spacer()
            NewMachineButton()
        }
    }

    private func launchPreset(_ preset: MachinePreset) async {
        isLaunching = true
        launchError = nil
        launchErrorPresetName = nil

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
            launchErrorPresetName = preset.name
            isLaunching = false
        }
    }
}

/// The "New Machine..." button shared by the Welcome window's Presets and
/// Machines pages: opens the custom machine builder, clearing any pending
/// connection target first so it starts fresh. Shared so both pages' buttons do
/// exactly the same thing.
struct NewMachineButton: View {
    @Environment(\.openWindow) private var openWindow

    var body: some View {
        Button("New Machine...") {
            let state = ConnectWindowState.shared
            state.pendingTarget = nil
            state.pendingNeedsRun = false
            state.pendingProvenanceUUID = nil
            openWindow(id: "new-machine")
        }
    }
}

/// The Welcome window's bottom action bar: the horizontal and bottom padding the
/// Presets and Machines pages share, so their buttons sit the same distance from
/// the window's edges rather than one hugging the bottom. Lay the buttons out
/// inside it with Spacers.
struct WelcomeBottomBar<Content: View>: View {
    @ViewBuilder var content: Content

    var body: some View {
        HStack {
            content
        }
        .padding(.horizontal, 24)
        .padding(.bottom, 16)
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
    let thumbnailCache: PresetThumbnailCache
    let action: () -> Void

    /// The preset's thumbnail once the cache has it; nil shows the placeholder.
    @State private var thumbnail: NSImage?

    var body: some View {
        Button(action: action) {
            VStack(spacing: 0) {
                // Thumbnail area
                thumbnailView
                    .frame(width: 160, height: 120)
                    .clipped()

                // Label area — fixed height so all cards are uniform.
                // Tall enough for a two-line name plus a two-line
                // description; top-aligned so shorter cards leave the
                // spare room below rather than floating their text.
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
                .frame(width: 144, height: 64, alignment: .top)
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
        .task(id: preset.id) {
            thumbnail = await thumbnailCache.thumbnail(for: preset)
        }
    }

    @ViewBuilder
    private var thumbnailView: some View {
        if let thumbnail {
            ZStack {
                Color.black
                Image(nsImage: thumbnail)
                    .resizable()
                    .interpolation(.high)
                    .antialiased(true)
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
}
