import SwiftUI

/// Main content view displaying the emulator output
struct ContentView: View {
    @StateObject private var videoClient = VideoClient()

    var body: some View {
        ZStack {
            // Emulator display (receives frames directly via videoClient.renderer)
            EmulatorView(videoClient: videoClient)

            // Status overlay when not connected
            if videoClient.connectionState != .connected {
                statusOverlay
            }
        }
        .frame(
            minWidth: 640,
            idealWidth: CGFloat(videoClient.frameWidth),
            minHeight: 480,
            idealHeight: CGFloat(videoClient.frameHeight)
        )
        .aspectRatio(4.0/3.0, contentMode: .fit)
        .onAppear {
            videoClient.connect()
        }
        .onDisappear {
            videoClient.disconnect()
        }
    }

    @ViewBuilder
    private var statusOverlay: some View {
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
                Text("Connecting to beebium-server...")
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
}

#if DEBUG
struct ContentView_Previews: PreviewProvider {
    static var previews: some View {
        ContentView()
    }
}
#endif
