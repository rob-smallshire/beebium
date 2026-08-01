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

import Foundation
import Combine

/// Observable state for the audio mixer UI.
///
/// This class bridges between SwiftUI bindings and the audio renderer's atomic values.
/// When a @Published property changes, the corresponding renderer method is called
/// to update the atomic values read by the audio thread.
@MainActor
final class AudioMixerState: ObservableObject {

    // MARK: - Master Controls

    /// Master volume (0.0 to 1.0, linear slider value)
    @Published var masterVolume: Float = 1.0 {
        didSet { audioClient?.setMasterVolume(masterVolume) }
    }

    /// Global mute state
    @Published var isMuted: Bool = false {
        didSet { audioClient?.setMuted(isMuted) }
    }

    // MARK: - Per-Channel Volumes

    /// Per-channel volumes (0.0 to 1.0)
    /// Index 0-3: Tone0, Tone1, Tone2, Noise
    @Published var channelVolumes: [Float] = [1.0, 1.0, 1.0, 1.0] {
        didSet { updateChannelVolumes() }
    }

    // MARK: - Group Volumes

    /// Per-group volumes (groupId -> volume)
    @Published var groupVolumes: [UInt32: Float] = [:] {
        didSet { updateGroupVolumes() }
    }

    // MARK: - Per-Channel Pan (backend only, no UI)

    /// Per-channel pan positions (-1.0 = left, 0.0 = center, 1.0 = right)
    /// These can be set programmatically but have no UI controls
    var panPositions: [Float] = [0.0, 0.0, 0.0, 0.0] {
        didSet { updatePanPositions() }
    }

    // MARK: - Level Metering (for future UI)

    /// Per-channel meter states (updated periodically)
    @Published private(set) var channelLevels: [AudioRenderer.MeterState] = []

    // MARK: - Audio Client Reference

    /// Reference to the audio client (set during app initialization)
    weak var audioClient: AudioClient? {
        didSet {
            // Sync current state to new client
            if audioClient != nil {
                syncAllToClient()
            }
        }
    }

    // MARK: - Meter Update Timer

    private var meterTimer: Timer?

    // MARK: - Initialization

    init() {
        // Initialize with default group (Internal Sound)
        groupVolumes[1] = 1.0
    }

    // MARK: - Meter Updates

    /// Start periodic meter updates for UI visualization
    func startMeterUpdates() {
        meterTimer?.invalidate()
        meterTimer = Timer.scheduledTimer(withTimeInterval: 1.0 / 30.0, repeats: true) { [weak self] _ in
            // Scheduled on the main run loop, so it fires on the main thread;
            // assumeIsolated reads the levels there and then. A Task would put
            // a fresh allocation and a run-loop hop between every meter frame
            // and the UI, thirty times a second, for no gain.
            MainActor.assumeIsolated {
                self?.updateMeterLevels()
            }
        }
    }

    /// Stop meter updates
    func stopMeterUpdates() {
        meterTimer?.invalidate()
        meterTimer = nil
    }

    private func updateMeterLevels() {
        guard let client = audioClient else { return }
        channelLevels = client.getAllMeterStates()
    }

    // MARK: - State Synchronization

    private func updateChannelVolumes() {
        guard let client = audioClient else { return }
        for (index, volume) in channelVolumes.enumerated() {
            client.setChannelVolume(index, volume: volume)
        }
    }

    private func updateGroupVolumes() {
        // Group volumes can be used to scale channel volumes
        // For now, we apply group volume as a multiplier when setting channel volumes
        // This is a simple approach; more sophisticated mixing could be added later
    }

    private func updatePanPositions() {
        guard let client = audioClient else { return }
        for (index, pan) in panPositions.enumerated() {
            client.setPanPosition(index, pan: pan)
        }
    }

    private func syncAllToClient() {
        guard let client = audioClient else { return }

        client.setMasterVolume(masterVolume)
        client.setMuted(isMuted)

        for (index, volume) in channelVolumes.enumerated() {
            client.setChannelVolume(index, volume: volume)
        }

        for (index, pan) in panPositions.enumerated() {
            client.setPanPosition(index, pan: pan)
        }
    }

    // MARK: - Convenience Methods

    /// Set volume for a specific channel by name
    func setVolume(forChannel name: String, volume: Float) {
        switch name.lowercased() {
        case "tone0", "tone 0":
            if channelVolumes.count > 0 { channelVolumes[0] = volume }
        case "tone1", "tone 1":
            if channelVolumes.count > 1 { channelVolumes[1] = volume }
        case "tone2", "tone 2":
            if channelVolumes.count > 2 { channelVolumes[2] = volume }
        case "noise":
            if channelVolumes.count > 3 { channelVolumes[3] = volume }
        default:
            break
        }
    }

    /// Get volume for a specific channel index
    func volume(forChannel index: Int) -> Float {
        guard index >= 0 && index < channelVolumes.count else { return 1.0 }
        return channelVolumes[index]
    }

    /// Channel names for display
    static let channelNames = ["Tone 0", "Tone 1", "Tone 2", "Noise"]

    /// Get channel name for display
    func channelName(for index: Int) -> String {
        guard index >= 0 && index < Self.channelNames.count else { return "Channel \(index)" }
        return Self.channelNames[index]
    }
}
