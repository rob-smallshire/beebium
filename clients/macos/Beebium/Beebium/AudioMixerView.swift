// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version. Beebium is distributed in the hope that
// it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details. You should have received a copy of the GNU General Public License along with
// Beebium. If not, see <https://www.gnu.org/licenses/>.

import SwiftUI

/// Audio mixer sidebar view with volume controls for all sound channels.
///
/// Layout:
/// - Master volume with mute button at top
/// - Group sections (e.g., "Internal Sound")
/// - Per-channel volume sliders within groups
struct AudioMixerView: View {
    @ObservedObject var audioClient: AudioClient
    @ObservedObject var mixerState: AudioMixerState

    var body: some View {
        if !audioClient.isLoaded {
            loadingView
        } else {
            mixerContentView
        }
    }

    // MARK: - Loading View

    private var loadingView: some View {
        VStack(spacing: 12) {
            if let error = audioClient.errorMessage {
                Image(systemName: "exclamationmark.triangle")
                    .font(.system(size: 24))
                    .foregroundColor(.yellow)
                Text("Audio Error")
                    .font(.headline)
                    .foregroundColor(.secondary)
                Text(error)
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .multilineTextAlignment(.center)
            } else {
                ProgressView()
                Text("Connecting...")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    // MARK: - Mixer Content

    private var mixerContentView: some View {
        ScrollView {
            VStack(spacing: 0) {
                // Master volume section
                masterVolumeSection
                    .padding(.horizontal, 12)
                    .padding(.vertical, 12)

                Divider()
                    .padding(.horizontal, 12)

                // Channel groups
                ForEach(audioClient.groups) { group in
                    groupSection(group)
                }
            }
            .padding(.vertical, 8)
        }
    }

    // MARK: - Master Volume Section

    private var masterVolumeSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("Master")
                    .font(.headline)
                Spacer()
                muteButton
            }

            VolumeSlider(
                value: $mixerState.masterVolume,
                isMuted: mixerState.isMuted
            )
        }
    }

    private var muteButton: some View {
        Button {
            mixerState.isMuted.toggle()
        } label: {
            Image(systemName: mixerState.isMuted ? "speaker.slash.fill" : "speaker.wave.2.fill")
                .font(.system(size: 16))
                .foregroundColor(mixerState.isMuted ? .red : .primary.opacity(0.6))
        }
        .buttonStyle(.borderless)
        .help(mixerState.isMuted ? "Unmute" : "Mute")
    }

    // MARK: - Group Section

    private func groupSection(_ group: ChannelGroupInfo) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            // Group header
            HStack {
                Text(group.name)
                    .font(.subheadline)
                    .fontWeight(.medium)
                    .foregroundColor(.secondary)
                Spacer()
            }
            .padding(.horizontal, 12)
            .padding(.top, 12)

            // Channels in this group
            ForEach(channelsInGroup(group.id), id: \.0) { index, name in
                channelRow(index: index, name: name)
            }
        }
    }

    private func channelsInGroup(_ groupId: UInt32) -> [(Int, String)] {
        // A group may span several sources (the SN76489 is two); list all of
        // their channels, concatenated in source-index order.
        AudioSourceInfo.channelsInGroup(audioClient.sources, groupId: groupId)
    }

    private func channelRow(index: Int, name: String) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(name)
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .frame(width: 50, alignment: .leading)

                VolumeSlider(
                    value: channelVolumeBinding(for: index),
                    isMuted: mixerState.isMuted
                )

                Text("\(Int((mixerState.channelVolumes[safe: index] ?? 1.0) * 100))%")
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .frame(width: 35, alignment: .trailing)
                    .monospacedDigit()
            }
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 2)
    }

    private func channelVolumeBinding(for index: Int) -> Binding<Float> {
        Binding(
            get: { mixerState.channelVolumes[safe: index] ?? 1.0 },
            set: { newValue in
                if index < mixerState.channelVolumes.count {
                    mixerState.channelVolumes[index] = newValue
                }
            }
        )
    }

}

// MARK: - Volume Slider Component

/// Horizontal volume slider with visual feedback
private struct VolumeSlider: View {
    @Binding var value: Float
    let isMuted: Bool

    var body: some View {
        Slider(value: $value, in: 0...1)
            .controlSize(.small)
            .opacity(isMuted ? 0.5 : 1.0)
            .disabled(isMuted)
    }
}

// MARK: - Safe Array Access

private extension Array {
    subscript(safe index: Int) -> Element? {
        guard index >= 0 && index < count else { return nil }
        return self[index]
    }
}

// MARK: - Preview

#if DEBUG
struct AudioMixerView_Previews: PreviewProvider {
    static var previews: some View {
        AudioMixerView(
            audioClient: AudioClient(),
            mixerState: AudioMixerState()
        )
        .frame(width: 220, height: 400)
        .background(Color(nsColor: .windowBackgroundColor))
    }
}
#endif
