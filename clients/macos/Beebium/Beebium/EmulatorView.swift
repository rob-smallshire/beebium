import SwiftUI
import MetalKit

/// SwiftUI wrapper for KeyboardMTKView that displays emulator video frames
/// and handles keyboard input
struct EmulatorView: NSViewRepresentable {
    /// Video client to wire up for direct frame updates
    @ObservedObject var videoClient: VideoClient

    /// Keyboard client for sending key events to server
    @ObservedObject var keyboardClient: KeyboardClient

    func makeNSView(context: Context) -> KeyboardMTKView {
        let mtkView = KeyboardMTKView()

        // Get the default Metal device
        guard let device = MTLCreateSystemDefaultDevice() else {
            fatalError("Metal is not supported on this device")
        }

        mtkView.device = device
        mtkView.colorPixelFormat = .bgra8Unorm
        mtkView.clearColor = MTLClearColor(red: 0, green: 0, blue: 0, alpha: 1)

        // Create renderer and set as delegate
        if let renderer = MetalRenderer(device: device) {
            context.coordinator.renderer = renderer
            mtkView.delegate = renderer

            // Wire up renderer to video client for direct frame updates
            videoClient.renderer = renderer
        }

        // Wire up keyboard client for key events
        mtkView.keyboardClient = keyboardClient

        // Enable display link for smooth updates
        mtkView.isPaused = false
        mtkView.enableSetNeedsDisplay = false
        mtkView.preferredFramesPerSecond = 60

        return mtkView
    }

    func updateNSView(_ nsView: KeyboardMTKView, context: Context) {
        // Frame updates now happen directly via VideoClient -> MetalRenderer
        // Keyboard client reference is stable, no update needed
    }

    func makeCoordinator() -> Coordinator {
        Coordinator()
    }

    class Coordinator {
        var renderer: MetalRenderer?
    }
}

#if DEBUG
struct EmulatorView_Previews: PreviewProvider {
    static var previews: some View {
        EmulatorView(videoClient: VideoClient(), keyboardClient: KeyboardClient())
            .frame(width: 736, height: 576)
    }
}
#endif
