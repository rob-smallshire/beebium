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

/// Main content view displaying the emulator output
struct ContentView: View {
    @StateObject private var videoClient = VideoClient()
    @StateObject private var keyboardClient = KeyboardClient()

    var body: some View {
        ZStack {
            // Emulator display (receives frames directly via videoClient.renderer)
            EmulatorView(videoClient: videoClient, keyboardClient: keyboardClient)

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
            keyboardClient.disconnect()
            videoClient.disconnect()
        }
        .onChange(of: videoClient.connectionState) { newState in
            // Connect keyboard client when video client connects
            if case .connected = newState, let channel = videoClient.channel {
                keyboardClient.connect(channel: channel)
            } else if case .disconnected = newState {
                keyboardClient.disconnect()
            } else if case .error = newState {
                keyboardClient.disconnect()
            }
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
