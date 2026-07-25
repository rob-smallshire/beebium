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

/// Identity of the Welcome window, shared so a beebium:// launch can find and
/// close the empty Welcome that SwiftUI auto-opens (see BeebiumApp.handleDeepLink).
enum WelcomeWindow {
    static let title = "Welcome to Beebium"
}

/// Routes the main WindowGroup to either welcome or emulator content based on whether
/// a connection target was pending when the window was created. This eliminates the need
/// for a separate Welcome Window scene and avoids the ghost Window menu entry caused by
/// SwiftUI's unconditional window creation on launch.
struct MainWindowRouter: View {
    @ObservedObject var keyboardMappingManager: KeyboardMappingManager
    @State private var connectionTarget: ConnectionTarget?
    @State private var needsRun: Bool = false
    @State private var provenanceUUID: String?
    @State private var showSidebar: Bool?
    @State private var currentWindow: NSWindow?

    var body: some View {
        Group {
            if let target = connectionTarget {
                ContentView(
                    initialTarget: target,
                    initialNeedsRun: needsRun,
                    initialProvenanceUUID: provenanceUUID,
                    initialShowSidebar: showSidebar,
                    keyboardMappingManager: keyboardMappingManager
                )
            } else {
                WelcomeWindowContent(onDismiss: { currentWindow?.close() })
                    .background(WindowAccessor(window: $currentWindow, configure: { window in
                        window.titlebarAppearsTransparent = true
                        window.titleVisibility = .hidden
                        window.title = WelcomeWindow.title
                        window.setContentSize(NSSize(width: 800, height: 640))
                        window.center()
                    }))
            }
        }
        .onAppear {
            let (target, run, prov, sidebar) = ConnectWindowState.shared.consumePendingTarget()
            connectionTarget = target
            needsRun = run
            provenanceUUID = prov
            showSidebar = sidebar
            if target != nil {
                NotificationCenter.default.post(name: .didOpenEmulatorWindow, object: nil)
            }
        }
    }
}

/// Main content view displaying the emulator output
struct ContentView: View {
    @StateObject private var videoClient = VideoClient()
    @StateObject private var keyboardClient = KeyboardClient()
    @StateObject private var systemClient = SystemClient()
    @StateObject private var indicatorClient = IndicatorClient()
    @StateObject private var discClient = DiscClient()
    @StateObject private var audioClient = AudioClient()
    @StateObject private var debuggerClient = DebuggerClient()
    @StateObject private var audioMixerState = AudioMixerState()
    @StateObject private var econetClient = EconetClient()
    @StateObject private var serialClient = SerialClient()
    @StateObject private var extensionUiClient = ExtensionUiClient()
    @StateObject private var peripheralsClient = PeripheralsClient()
    @StateObject private var transportsClient = EconetTransportsClient()
    @StateObject private var sidewaysClient = SidewaysClient()
    @StateObject private var videoSettings = VideoSettings.loadFromUserDefaults()
    @StateObject private var speedModel = SpeedControlModel()
    @StateObject private var pasteCoordinator = PasteCoordinator()
    @StateObject private var selectionCoordinator = SelectionCoordinator()
    private let selectionPasteboard = SystemSelectionPasteboard()
    let initialTarget: ConnectionTarget
    let initialNeedsRun: Bool
    let initialProvenanceUUID: String?
    /// Initial sidebar visibility (e.g. from a beebium://…&sidebar=closed launch);
    /// nil leaves the default. Applied once in onAppear.
    let initialShowSidebar: Bool?
    /// Whether this window needs to call Run() after connection (for cores launched with --wait=api).
    /// Initialised from initialNeedsRun in onAppear; reset to false after Run() succeeds.
    @State private var needsRun: Bool = false
    @State private var showStatusBar: Bool = true
    @State private var showSidebar: Bool = true
    @State private var isImmersive: Bool = false
    @State private var preImmersiveShowSidebar: Bool = true
    @State private var preImmersiveShowStatusBar: Bool = true
    /// The NSWindow this ContentView is hosted in, captured once and never updated.
    /// `currentWindow` (set asynchronously by `WindowAccessor.onWindowChanged`) is
    /// transiently nil during the native fullscreen exit animation when AppKit moves
    /// views through bridge windows -- which would otherwise cause Immersive Mode
    /// enter/exit to operate on the wrong window or not at all on the second cycle.
    @State private var stableWindow: NSWindow?
    @ObservedObject var keyboardMappingManager: KeyboardMappingManager
    @State private var sidebarMode: SidebarMode = .storage
    @State private var currentWindow: NSWindow?
    @State private var closeCoordinator: WindowCloseCoordinator?
    @State private var lastSeenMachineUUID: String = ""
    private let clientGroup = ClientGroup()
    private let videoSettingsCache = VideoSettingsCache.shared
    @Environment(\.openWindow) private var openWindow

    private var columnVisibility: Binding<NavigationSplitViewVisibility> {
        Binding(
            get: { showSidebar ? .all : .detailOnly },
            set: { showSidebar = ($0 != .detailOnly) }
        )
    }

    private var sidebarPanel: some View {
        VStack(spacing: 0) {
            SidebarModeToolbar(selectedMode: $sidebarMode)
            Divider()
            SidebarModeContent(
                mode: sidebarMode,
                discClient: discClient,
                indicatorClient: indicatorClient,
                keyboardMappingManager: keyboardMappingManager,
                audioClient: audioClient,
                audioMixerState: audioMixerState,
                econetClient: econetClient,
                serialClient: serialClient,
                extensionUiClient: extensionUiClient,
                peripheralsClient: peripheralsClient,
                transportsClient: transportsClient,
                sidewaysClient: sidewaysClient,
                videoSettings: videoSettings,
                speedModel: speedModel
            )
        }
        .background(Color(nsColor: .windowBackgroundColor))
    }

    private var emulatorView: some View {
        EmulatorView(
            videoClient: videoClient,
            keyboardClient: keyboardClient,
            indicatorClient: indicatorClient,
            videoSettings: videoSettings,
            bbcKeyCache: keyboardMappingManager.bbcKeyCache,
            pasteCoordinator: pasteCoordinator,
            selectionCoordinator: selectionCoordinator
        )
    }

    private var statusBar: some View {
        StatusBarView(
            systemClient: systemClient,
            indicatorClient: indicatorClient,
            keyboardClient: keyboardClient,
            keyboardMappingManager: keyboardMappingManager,
            machineManager: MachineManager.shared,
            connectionTarget: videoClient.target,
            onToggleUnlink: { toggleUnlinkCurrentMachine() }
        )
    }

    private var navigationLayout: some View {
        NavigationSplitView(columnVisibility: columnVisibility) {
            sidebarPanel
                .navigationSplitViewColumnWidth(min: 180, ideal: 260, max: 500)
        } detail: {
            VStack(spacing: 0) {
                ZStack {
                    emulatorView

                    if videoClient.connectionState != .connected
                        || systemClient.liveness != .active {
                        statusOverlay
                    }
                }

                if showStatusBar {
                    statusBar
                }
            }
            .frame(minWidth: 320, minHeight: 240)
        }
        .navigationSplitViewStyle(.balanced)
        .animation(.default, value: showSidebar)
    }

    private var immersiveLayout: some View {
        ZStack(alignment: .topLeading) {
            emulatorView
                .ignoresSafeArea()

            // Sidebar and status bar are always in the view tree; visibility is
            // driven by .offset / .opacity rather than `if` + `.transition`.
            // SwiftUI's conditional-plus-transition combo can drop the removal
            // animation, leaving a jarring asymmetry where reveal slides in but
            // hide vanishes instantly. Translating an always-present view animates
            // symmetrically in both directions.
            HStack(spacing: 0) {
                sidebarPanel
                    .frame(width: 280)
                Spacer(minLength: 0)
            }
            .offset(x: showSidebar ? 0 : -280)
            .opacity(showSidebar ? 1 : 0)
            .allowsHitTesting(showSidebar)

            VStack(spacing: 0) {
                Spacer(minLength: 0)
                statusBar
                    .background(Color(nsColor: .windowBackgroundColor))
            }
            .offset(y: showStatusBar ? 0 : 100)
            .opacity(showStatusBar ? 1 : 0)
            .allowsHitTesting(showStatusBar)
        }
        .animation(.default, value: showSidebar)
        .animation(.default, value: showStatusBar)
    }

    var body: some View {
        Group {
            if isImmersive {
                immersiveLayout
            } else {
                navigationLayout
            }
        }
        .navigationTitle("Beebium")
        .alert(
            "Server / app version mismatch",
            isPresented: Binding(
                get: { systemClient.protocolMismatchMessage != nil },
                set: { presented in
                    if !presented { systemClient.protocolMismatchMessage = nil }
                }
            ),
            presenting: systemClient.protocolMismatchMessage
        ) { _ in
            Button("OK", role: .cancel) { systemClient.protocolMismatchMessage = nil }
        } message: { message in
            Text(message)
        }
        .focusedValue(\.pasteCoordinator, pasteCoordinator)
        .focusedValue(\.selectionCoordinator, selectionCoordinator)
        .onAppear {
            // The coordinator holds these weakly; ContentView owns them for
            // the window's lifetime.
            pasteCoordinator.typist = keyboardClient
            pasteCoordinator.speedControl = speedModel
            pasteCoordinator.audioMute = audioMixerState
            pasteCoordinator.realTimeTransportActive = { econetClient.requiresRealTime }

            // The selection coordinator holds these weakly too. Its
            // geometrySource is the Metal renderer, wired in EmulatorView where
            // the renderer is created.
            selectionCoordinator.textService = videoClient
            selectionCoordinator.freezer = videoClient
            selectionCoordinator.pasteboard = selectionPasteboard

            // Apply an initial sidebar state requested at launch (e.g. a
            // beebium://…&sidebar=closed connect URL); nil leaves the default.
            if let initialShowSidebar = initialShowSidebar {
                showSidebar = initialShowSidebar
            }
        }
        .focusedValue(\.sidebarMode, $sidebarMode)
        .focusedValue(\.showSidebar, $showSidebar)
        .focusedValue(\.showStatusBar, $showStatusBar)
        .focusedValue(\.isImmersive, $isImmersive)
        .onChange(of: isImmersive) { newValue in
            guard let window = stableWindow else { return }
            if newValue {
                preImmersiveShowSidebar = showSidebar
                preImmersiveShowStatusBar = showStatusBar
                showSidebar = false
                showStatusBar = false
                ImmersiveCoordinator.shared.enterImmersive(window: window)
            } else {
                ImmersiveCoordinator.shared.exitImmersive(window: window)
                showSidebar = preImmersiveShowSidebar
                showStatusBar = preImmersiveShowStatusBar
            }
        }
        .onReceive(NotificationCenter.default.publisher(
            for: .immersiveCoordinatorDidExitUnexpectedly)
        ) { notification in
            // Native fullscreen ended through some path other than our menu item
            // (e.g. the green button on hover, or Cmd+Ctrl+F). Bring isImmersive back
            // into agreement with the window's actual state, which restores
            // navigationLayout and the pre-immersive sidebar / status-bar visibility.
            if let window = notification.object as? NSWindow,
               window === stableWindow,
               isImmersive {
                isImmersive = false
            }
        }
        .focusedValue(\.openNewWindow) { openWindow(id: "main") }
        .onAppear {
            // Capture openWindow into shared AppActions for FileCommands
            AppActions.shared.openNewMachine = { [openWindow] in openWindow(id: "new-machine") }
            AppActions.shared.openConnect = { [openWindow] in openWindow(id: "connect") }
            AppActions.shared.openWelcome = { [openWindow] in
                ConnectWindowState.shared.pendingTarget = nil
                ConnectWindowState.shared.pendingNeedsRun = false
                ConnectWindowState.shared.pendingProvenanceUUID = nil
                openWindow(id: "main")
            }

            // Wire up keyboard client to mapping manager
            keyboardClient.mappingManager = keyboardMappingManager

            // Wire up audio mixer state to audio client
            audioMixerState.audioClient = audioClient

            // Initial Caps Lock sync. This callback fires once per session
            // when the indicator stream delivers its first caps-lock-led
            // value -- the moment when {channel up, keyboard service
            // connected, BBC LED state actually known} are all true. Treat
            // it as the canonical "perform initial sync now" event. Other
            // triggers in this view (NSWindow.didBecomeKey, the at-connect
            // sync below) gate on indicatorClient.hasTriggeredInitialSync
            // so they never run before this one.
            indicatorClient.onInitialCapsLockSync = { [weak keyboardClient] in
                guard let keyboardClient = keyboardClient else { return }
                let macCapsLockIsOn = NSEvent.modifierFlags.contains(.capsLock)
                keyboardClient.syncCapsLockState(macCapsLockIsOn: macCapsLockIsOn)
            }

            // Wire the speed control to the system client for its RPCs.
            speedModel.bind(to: systemClient)

            // Register clients with ClientGroup for bulk disconnect
            clientGroup.register(keyboardClient)
            clientGroup.register(systemClient)
            clientGroup.register(indicatorClient)
            clientGroup.register(discClient)
            clientGroup.register(audioClient)
            clientGroup.register(debuggerClient)
            clientGroup.register(econetClient)
            clientGroup.register(serialClient)
            clientGroup.register(extensionUiClient)
            clientGroup.register(peripheralsClient)
            clientGroup.register(transportsClient)
            clientGroup.register(sidewaysClient)
            clientGroup.registerVideoClient(videoClient)

            // Connection target was passed from MainWindowRouter (which consumed the
            // pending target from ConnectWindowState). Connect immediately.
            needsRun = initialNeedsRun
            videoClient.reconnect(to: initialTarget)
        }
        // No onDisappear: SwiftUI's WindowGroup does not fire onDisappear on window
        // close. All cleanup (shutdown decisions, client disconnection) is handled by
        // WindowCloseCoordinator, which intercepts both the close button and Cmd+W.
        .background(WindowAccessor(window: $currentWindow))
        .onChange(of: currentWindow) { window in
            guard let window = window else { return }
            // Capture the NSWindow once; never update it. Used by Immersive Mode to
            // sidestep the transient-nil currentWindow during fullscreen transitions.
            if stableWindow == nil {
                stableWindow = window
            }
            // Install close-button interception for the multi-client dialog case.
            // SwiftUI's WindowGroup manages its own window delegate, so we cannot
            // rely on windowShouldClose. Instead, redirect the close button's action
            // to our coordinator, which can show an alert and prevent/allow the close.
            if closeCoordinator == nil {
                let coordinator = WindowCloseCoordinator(
                    systemClient: systemClient,
                    videoClient: videoClient,
                    machineManager: MachineManager.shared,
                    window: window,
                    clientGroup: clientGroup
                )
                coordinator.install(on: window)
                closeCoordinator = coordinator
            }
        }
        .onChange(of: videoClient.connectionState) { newState in
            // Immersive Mode is only meaningful while emulation is live. Any
            // transition out of .connected (network blip, server shutdown,
            // user-initiated disconnect) returns the window to its normal
            // chrome so the user can see what's happening and act.
            if isImmersive, case .connected = newState {
                // still connected -- no-op
            } else if isImmersive {
                isImmersive = false
            }

            // Connect clients when video client connects
            if case .connected = newState, let channel = videoClient.channel {
                keyboardClient.connect(channel: channel)
                systemClient.connect(channel: channel, provenanceUUID: initialProvenanceUUID)
                indicatorClient.connect(channel: channel)
                sidewaysClient.connect(channel: channel)
                discClient.connect(channel: channel)
                audioClient.connect(channel: channel)
                debuggerClient.connect(channel: channel)
                econetClient.connect(channel: channel)
                serialClient.connect(channel: channel)
                extensionUiClient.connect(channel: channel)
                peripheralsClient.connect(channel: channel)
                transportsClient.connect(channel: channel)

                // Register this connection
                if let window = currentWindow {
                    ConnectionRegistry.shared.register(
                        address: videoClient.target.address,
                        window: window
                    )
                }

                if closeCoordinator == nil {
                    NSLog("[ContentView] WARNING: closeCoordinator is nil at connection time")
                }

                // If this was a freshly launched core with --wait=api, start emulation
                if needsRun {
                    needsRun = false
                    Task {
                        do {
                            try await debuggerClient.run()
                        } catch {
                            NSLog("[ContentView] Failed to start emulation: \(error)")
                        }
                    }
                }

                // Load keyboard mappings from core
                Task {
                    await keyboardClient.loadKeyMappings()
                }

                // Re-sync Caps Lock if and only if the indicator stream
                // has already delivered LED state in this session (i.e.
                // this is a reconnect, not first startup). On first
                // startup the LED state has not arrived yet, so sync
                // would no-op silently against the .off default; the
                // canonical initial sync via
                // indicatorClient.onInitialCapsLockSync will fire
                // shortly afterwards with real data.
                if indicatorClient.hasTriggeredInitialSync {
                    let macCapsLockIsOn = NSEvent.modifierFlags.contains(.capsLock)
                    keyboardClient.syncCapsLockState(
                        macCapsLockIsOn: macCapsLockIsOn
                    )
                }
            } else {
                // Handle unexpected disconnection (server dropped connection).
                // IndicatorClient resets its hasTriggeredInitialSync inside
                // disconnect(), so the gate naturally re-arms for reconnect.
                if case .disconnected = newState {
                    ConnectionRegistry.shared.unregister(address: videoClient.target.address)
                    clientGroup.disconnectNonVideoClients()
                } else if case .error = newState {
                    ConnectionRegistry.shared.unregister(address: videoClient.target.address)
                    clientGroup.disconnectNonVideoClients()
                }
            }
        }
        .onChange(of: systemClient.liveness) { newLiveness in
            // A mid-session loss/stop must surface its overlay; immersive mode
            // hides the chrome, so drop back to the normal layout where the
            // overlay lives (mirrors the connectionState handler above).
            if isImmersive, newLiveness != .active {
                isImmersive = false
            }
        }
        .onReceive(NotificationCenter.default.publisher(for: NSWindow.didBecomeKeyNotification)) { _ in
            // Sync Caps Lock state when window gains focus (macOS Caps Lock
            // may have changed while we were unfocused). Only fire after
            // the indicator stream has delivered LED state -- otherwise
            // the window-key notification at app launch (which happens
            // before the channel is up and before the indicator stream
            // has delivered LED data) would attempt a sync against
            // missing client and stale state.
            guard indicatorClient.hasTriggeredInitialSync else { return }
            let macCapsLockIsOn = NSEvent.modifierFlags.contains(.capsLock)
            keyboardClient.syncCapsLockState(
                macCapsLockIsOn: macCapsLockIsOn
            )
        }
        .onChange(of: systemClient.clientCount) { count in
            // Keep MachineManager's cached client count in sync for the quit handler
            MachineManager.shared.updateClientCount(
                address: videoClient.target.address,
                count: count
            )
        }
        .onChange(of: keyboardMappingManager.isCapsLockSyncEnabled) { isEnabled in
            // Sync immediately when user enables Caps Lock sync
            if isEnabled {
                let macCapsLockIsOn = NSEvent.modifierFlags.contains(.capsLock)
                keyboardClient.syncCapsLockState(
                    macCapsLockIsOn: macCapsLockIsOn
                )
            }
        }
        // Per-machine VideoSettings cache: when SystemClient receives the
        // server's MachineIdentity for the first time (or for a different
        // machine after a reconnect), look the UUID up in the cache and
        // restore any previously-saved per-window settings for that machine.
        .onChange(of: systemClient.machineUUID) { uuid in
            guard !uuid.isEmpty, uuid != lastSeenMachineUUID else { return }
            lastSeenMachineUUID = uuid
            if let snapshot = videoSettingsCache.snapshot(forMachineUUID: uuid) {
                videoSettings.apply(snapshot)
            }
        }
        // Save the current per-window video settings back to the cache on
        // every change so a sibling window opened on the same machine picks
        // up the latest values immediately. The empty-UUID guard inside
        // VideoSettingsCache.save makes pre-connection edits a no-op.
        .onChange(of: videoSettings.activeStyleID) { _ in saveVideoSettingsSnapshot() }
        .onChange(of: videoSettings.pixelShape) { _ in saveVideoSettingsSnapshot() }
        .onChange(of: videoSettings.windowBackground) { _ in saveVideoSettingsSnapshot() }
    }

    private func saveVideoSettingsSnapshot() {
        let uuid = systemClient.machineUUID
        guard !uuid.isEmpty else { return }
        videoSettingsCache.save(videoSettings.makeSnapshot(), forMachineUUID: uuid)
    }

    /// Toggle the deferred unlink request for this window's machine
    private func toggleUnlinkCurrentMachine() {
        if let machine = MachineManager.shared.machine(forTarget: videoClient.target) {
            MachineManager.shared.setUnlinkRequested(id: machine.id, requested: !machine.unlinkRequested)
        }
    }

    /// Connect to a different machine target
    private func connectTo(_ target: ConnectionTarget) {
        // Reconnect video client to new target
        // The onChange handler will automatically cascade to other clients
        videoClient.reconnect(to: target)
    }

    /// A label identifying which emulator this window is connected to, for the
    /// disconnection overlay. The server's friendly name is preferred; the
    /// target address is a stable fallback (and survives if the name is later
    /// cleared by teardown).
    private var connectionLabel: String {
        systemClient.machineDisplayName.isEmpty
            ? videoClient.target.address
            : systemClient.machineDisplayName
    }

    @ViewBuilder
    private var statusOverlay: some View {
        // Server liveness (from the WatchServerStatus status facility) takes
        // priority: a mid-session loss or shutdown is what the user most needs
        // to see, and it is distinct from connect-time states.
        switch systemClient.liveness {
        case .died:
            // The server process ended unexpectedly (crash/kill). We know it was
            // the process, not the network, so we say so -- and there is nothing
            // to reconnect to, so the only honest action is to close the window.
            disconnectionOverlay(
                systemImage: "exclamationmark.triangle.fill",
                tint: .orange,
                title: "Emulator stopped unexpectedly",
                message: "\(connectionLabel) ended unexpectedly -- it may have "
                    + "crashed or been killed. There is nothing to reconnect to."
            )
        case .stopped:
            // A graceful shutdown: the emulator is intentionally gone. Just
            // inform and let the user close the window.
            disconnectionOverlay(
                systemImage: "stop.circle.fill",
                tint: .secondary,
                title: "Emulator stopped",
                message: "\(connectionLabel) was deliberately shut down. There "
                    + "is nothing to reconnect to."
            )
        case .unreachable:
            // Heartbeats stopped but the stream is still open: the server may be
            // unreachable temporarily (network blip). This is recoverable -- it
            // clears itself if heartbeats resume -- so show a spinner and a
            // single button to give up rather than a terminal message.
            reconnectingOverlay(label: connectionLabel)
        case .active:
            connectStatusCard
        }
    }

    /// Connect-time states (initial connect / connect failure). These are not
    /// mid-session losses, so they keep the lighter, undimmed card.
    @ViewBuilder
    private var connectStatusCard: some View {
        VStack(spacing: 16) {
            switch videoClient.connectionState {
            case .disconnected:
                Text("Disconnected")
                    .font(.headline)
                Button("Connect") {
                    videoClient.connect()
                }

            case .connecting:
                ProgressView()
                    .scaleEffect(1.5)
                Text("Connecting to \(videoClient.target.address)...")
                    .font(.headline)

            case .connected:
                EmptyView()

            case .error(let message):
                Image(systemName: "exclamationmark.triangle")
                    .font(.system(size: 48))
                    .foregroundColor(.yellow)
                Text("Connection Error")
                    .font(.headline)
                Text(message)
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .multilineTextAlignment(.center)
                    .padding(.horizontal)
                Button("Retry") {
                    videoClient.disconnect()
                    videoClient.connect()
                }
                .padding(.top, 8)
            }
        }
        .padding(32)
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 16))
    }

    /// Prominent, dimmed overlay for a mid-session disconnection. Dims the
    /// frozen last frame (so it reads as "stopped") while keeping it visible
    /// underneath, so you can still tell which emulator this window is.
    private func disconnectionOverlay(systemImage: String, tint: Color,
                                      title: String, message: String) -> some View {
        ZStack {
            Color.black.opacity(0.5)
                .ignoresSafeArea()

            VStack(spacing: 14) {
                Image(systemName: systemImage)
                    .font(.system(size: 52))
                    .foregroundColor(tint)
                Text(title)
                    .font(.title2.weight(.semibold))
                Text(message)
                    .font(.callout)
                    .foregroundColor(.secondary)
                    .multilineTextAlignment(.center)
                    .fixedSize(horizontal: false, vertical: true)
                // The server is gone either way -- there is no recovery, so the
                // only action is to close the window.
                Button("Close") {
                    closeWindow()
                }
                .keyboardShortcut(.defaultAction)
                .padding(.top, 6)
            }
            .padding(36)
            .frame(maxWidth: 420)
            .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 16))
            .shadow(radius: 24)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    /// Transient, recoverable disconnection: the server stopped heartbeating but
    /// the stream is still open. Auto-recovers when heartbeats resume; a single
    /// button lets the user give up and close the window.
    private func reconnectingOverlay(label: String) -> some View {
        ZStack {
            Color.black.opacity(0.5)
                .ignoresSafeArea()

            VStack(spacing: 14) {
                ProgressView()
                    .scaleEffect(1.4)
                    .padding(.bottom, 4)
                Text("Connection lost")
                    .font(.title2.weight(.semibold))
                Text("Trying to reconnect to \(label)\u{2026}")
                    .font(.callout)
                    .foregroundColor(.secondary)
                    .multilineTextAlignment(.center)
                    .fixedSize(horizontal: false, vertical: true)
                Button("Close") {
                    closeWindow()
                }
                .padding(.top, 6)
            }
            .padding(36)
            .frame(maxWidth: 420)
            .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 16))
            .shadow(radius: 24)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    /// Close this window through the same path as the title-bar close button,
    /// so it runs the WindowCloseCoordinator's teardown rather than a raw close.
    private func closeWindow() {
        guard let window = currentWindow ?? stableWindow else { return }
        if let closeButton = window.standardWindowButton(.closeButton),
           let action = closeButton.action, let target = closeButton.target {
            _ = NSApp.sendAction(action, to: target, from: closeButton)
        } else {
            window.performClose(nil)
        }
    }
}

#if DEBUG
struct ContentView_Previews: PreviewProvider {
    static var previews: some View {
        ContentView(
            initialTarget: ConnectionTarget(host: "127.0.0.1", port: 50051),
            initialNeedsRun: false,
            initialProvenanceUUID: nil,
            initialShowSidebar: nil,
            keyboardMappingManager: KeyboardMappingManager()
        )
    }
}
#endif

// MARK: - Window Accessor

/// NSView subclass that reports window attachment synchronously via viewDidMoveToWindow(),
/// firing before the window is rendered on screen.
class WindowObserverView: NSView {
    var onWindowChanged: ((NSWindow?) -> Void)?

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        onWindowChanged?(window)
    }
}

/// Helper view to capture the NSWindow reference from SwiftUI into an @Binding.
/// Uses viewDidMoveToWindow for synchronous capture before the first render.
/// The optional `configure` closure runs synchronously in viewDidMoveToWindow,
/// allowing window properties (size, title bar style, etc.) to be set before
/// the window is first displayed on screen.
struct WindowAccessor: NSViewRepresentable {
    @Binding var window: NSWindow?
    var configure: ((NSWindow) -> Void)?

    func makeNSView(context: Context) -> WindowObserverView {
        let view = WindowObserverView()
        view.onWindowChanged = { [configure] newWindow in
            if let newWindow = newWindow {
                configure?(newWindow)
            }
            DispatchQueue.main.async {
                self.window = newWindow
            }
        }
        return view
    }

    func updateNSView(_ nsView: WindowObserverView, context: Context) {
        nsView.onWindowChanged = { [configure] newWindow in
            if let newWindow = newWindow {
                configure?(newWindow)
            }
            DispatchQueue.main.async {
                self.window = newWindow
            }
        }
    }
}

// MARK: - Window Close Coordinator

/// Intercepts the window close button to implement lifecycle-aware behavior.
///
/// SwiftUI's WindowGroup manages its own NSWindowDelegate, so we cannot rely on
/// windowShouldClose. Instead, this coordinator redirects the close button's
/// target/action to itself, enabling pre-close logic.
///
/// All decision logic is delegated to MachineManager.windowCloseAction().
/// This coordinator only handles the UI flow: close immediately, or show
/// a multi-client alert before closing.
@MainActor
class WindowCloseCoordinator: NSObject {
    let systemClient: SystemClient
    let videoClient: VideoClient
    let machineManager: MachineManager
    let clientGroup: ClientGroup
    weak var window: NSWindow?

    init(systemClient: SystemClient, videoClient: VideoClient, machineManager: MachineManager, window: NSWindow, clientGroup: ClientGroup) {
        self.systemClient = systemClient
        self.videoClient = videoClient
        self.machineManager = machineManager
        self.clientGroup = clientGroup
        self.window = window
    }

    /// Redirect the window's close button to our handler
    func install(on window: NSWindow) {
        if let closeButton = window.standardWindowButton(.closeButton) {
            closeButton.target = self
            closeButton.action = #selector(handleCloseButton(_:))
        } else {
            NSLog("[WindowCloseCoordinator] WARNING: No close button found on window '%@'",
                  window.title)
        }
    }

    /// Check whether this coordinator is still installed as the close button target
    func verifyInstallation() -> Bool {
        guard let window = window,
              let closeButton = window.standardWindowButton(.closeButton) else {
            return false
        }
        return closeButton.target === self
    }

    @objc private func handleCloseButton(_ sender: Any?) {
        guard let window = window else {
            NSLog("[WindowCloseCoordinator] handleCloseButton: window is nil")
            return
        }
        let address = videoClient.target.address
        let clientCount = systemClient.clientCount
        NSLog("[WindowCloseCoordinator] handleCloseButton for address %@, clientCount %d", address, clientCount)

        let action = machineManager.windowCloseAction(forAddress: address, clientCount: clientCount)

        switch action {
        case .shutdownServer:
            // Disconnect gRPC clients first — the server's graceful shutdown
            // waits for active streams to close, so we must drop them before
            // sending SIGTERM.
            ConnectionRegistry.shared.unregister(address: address)
            clientGroup.disconnectAll()
            machineManager.shutdownServer(forAddress: address)
            window.close()
        case .disconnect:
            // Finalize any pending unlink request
            if let machine = machineManager.machine(forAddress: address), machine.unlinkRequested {
                machineManager.unlink(id: machine.id)
            }
            ConnectionRegistry.shared.unregister(address: address)
            clientGroup.disconnectAll()
            window.close()
        case .promptUser(let count):
            showMultiClientAlert(window: window, address: address, clientCount: count)
        }
    }

    @MainActor
    private func showMultiClientAlert(window: NSWindow, address: String, clientCount: Int) {
        let machineName = systemClient.machineDisplayName.isEmpty
            ? "This machine"
            : "\"\(systemClient.machineDisplayName)\""

        let alert = NSAlert()
        alert.messageText = "\(machineName) has \(clientCount - 1) other connected client\(clientCount - 1 == 1 ? "" : "s")."
        alert.informativeText = "You can shut down the machine (disconnecting other clients) or leave it running."
        alert.alertStyle = .informational

        let shutDownButton = alert.addButton(withTitle: "Shut Down")
        shutDownButton.hasDestructiveAction = true
        alert.addButton(withTitle: "Leave Running")
        alert.addButton(withTitle: "Cancel")

        alert.beginSheetModal(for: window) { [weak self] response in
            guard let self = self else { return }

            Task { @MainActor in
                switch response {
                case .alertFirstButtonReturn:
                    // Shut Down: gRPC shutdown (notifies other clients), then close
                    await self.machineManager.gracefulShutdownServer(forAddress: address, using: self.systemClient)
                    ConnectionRegistry.shared.unregister(address: address)
                    self.clientGroup.disconnectAll()
                    window.close()
                case .alertSecondButtonReturn:
                    // Leave Running: unlink, then close
                    if let machine = self.machineManager.machine(forAddress: address) {
                        self.machineManager.unlink(id: machine.id)
                    }
                    ConnectionRegistry.shared.unregister(address: address)
                    self.clientGroup.disconnectAll()
                    window.close()
                default:
                    break
                }
            }
        }
    }
}
