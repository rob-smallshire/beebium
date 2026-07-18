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
import MetalKit

/// SwiftUI wrapper for KeyboardMTKView that displays emulator video frames
/// and handles keyboard input
struct EmulatorView: NSViewRepresentable {
    /// Video client to wire up for direct frame updates
    @ObservedObject var videoClient: VideoClient

    /// Keyboard client for sending key events to server
    @ObservedObject var keyboardClient: KeyboardClient

    /// Indicator client for LED states (used by Touch Bar)
    @ObservedObject var indicatorClient: IndicatorClient

    /// Per-window video settings (active display style, etc.).
    /// Changes are propagated to the renderer via updateNSView.
    @ObservedObject var videoSettings: VideoSettings

    /// BBC key cache for Touch Bar key lookups
    var bbcKeyCache: BBCKeyCache?

    /// Types pasted text on the emulated keyboard.
    var pasteCoordinator: PasteCoordinator?

    func makeNSView(context: Context) -> KeyboardMTKView {
        let mtkView = KeyboardMTKView()

        // Get the default Metal device
        guard let device = MTLCreateSystemDefaultDevice() else {
            fatalError("Metal is not supported on this device")
        }

        mtkView.device = device
        mtkView.colorPixelFormat = .bgra8Unorm
        mtkView.clearColor = videoSettings.windowBackground.mtlClearColor

        // Create renderer with the currently selected display style as the
        // initial pipeline. setActiveStyle handles subsequent style changes.
        if let renderer = MetalRenderer(device: device,
                                         initialStyle: videoSettings.activeStyle) {
            renderer.parScale = videoSettings.parScale
            context.coordinator.renderer = renderer
            mtkView.delegate = renderer

            // Wire up renderer to video client for direct frame updates
            videoClient.renderer = renderer
        }

        // Wire up keyboard client for key events
        mtkView.keyboardClient = keyboardClient
        mtkView.pasteCoordinator = pasteCoordinator

        // Wire up indicator client and Touch Bar manager
        mtkView.indicatorClient = indicatorClient
        if #available(macOS 10.12.2, *) {
            let touchBarManager = BeebiumTouchBarManager(
                keyboardClient: keyboardClient,
                indicatorClient: indicatorClient,
                bbcKeyCache: bbcKeyCache
            )
            mtkView.touchBarManager = touchBarManager
            context.coordinator.touchBarManager = touchBarManager
        }

        // Enable display link for smooth updates
        mtkView.isPaused = false
        mtkView.enableSetNeedsDisplay = false
        mtkView.preferredFramesPerSecond = 60

        return mtkView
    }

    func updateNSView(_ nsView: KeyboardMTKView, context: Context) {
        // Frame updates happen directly via VideoClient -> MetalRenderer.
        // The keyboard client reference is stable. We do need to react to
        // display-style, pixel-shape, and window-background changes from the
        // sidebar.
        nsView.clearColor = videoSettings.windowBackground.mtlClearColor
        guard let renderer = context.coordinator.renderer else { return }
        renderer.setActiveStyle(videoSettings.activeStyle)
        renderer.parScale = videoSettings.parScale
    }

    func makeCoordinator() -> Coordinator {
        Coordinator()
    }

    class Coordinator {
        var renderer: MetalRenderer?
        @available(macOS 10.12.2, *)
        var touchBarManager: BeebiumTouchBarManager?
    }
}

#if DEBUG
struct EmulatorView_Previews: PreviewProvider {
    static var previews: some View {
        EmulatorView(
            videoClient: VideoClient(),
            keyboardClient: KeyboardClient(),
            indicatorClient: IndicatorClient(),
            videoSettings: VideoSettings(),
            bbcKeyCache: nil
        )
            .frame(width: 736, height: 576)
    }
}
#endif
