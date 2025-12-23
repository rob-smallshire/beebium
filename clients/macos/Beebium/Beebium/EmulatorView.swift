import SwiftUI
import MetalKit

/// SwiftUI wrapper for MTKView that displays emulator video frames
struct EmulatorView: NSViewRepresentable {
    /// Video client to wire up for direct frame updates
    @ObservedObject var videoClient: VideoClient

    func makeNSView(context: Context) -> MTKView {
        let mtkView = MTKView()

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

        // Enable display link for smooth updates
        mtkView.isPaused = false
        mtkView.enableSetNeedsDisplay = false
        mtkView.preferredFramesPerSecond = 60

        return mtkView
    }

    func updateNSView(_ nsView: MTKView, context: Context) {
        // Frame updates now happen directly via VideoClient -> MetalRenderer
        // This method is kept for potential future use (e.g., resize handling)
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
        EmulatorView(videoClient: VideoClient())
            .frame(width: 736, height: 576)
    }
}
#endif
