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
import GRPC

/// Audio source information parsed from server
struct AudioSourceInfo: Identifiable {
    let id: UInt32
    let name: String
    let channelNames: [String]
    let groupId: UInt32
}

/// Channel group information for UI organization
struct ChannelGroupInfo: Identifiable {
    let id: UInt32
    let name: String
    let description: String
    let color: String
}

/// Client for streaming audio from beebium-server via gRPC.
///
/// Follows the established client pattern:
/// - @MainActor for UI updates
/// - Connects via shared GRPCChannel
/// - Manages audio streaming, format metadata, and channel introspection
@MainActor
final class AudioClient: ObservableObject, Disconnectable {

    // MARK: - Audio Format Metadata

    /// Sample rate from server (typically 48000)
    @Published private(set) var sampleRate: UInt32 = 48000

    /// Number of 32-bit source fields per sample
    @Published private(set) var sourceCount: UInt32 = 4

    /// Audio source metadata
    @Published private(set) var sources: [AudioSourceInfo] = []

    /// Channel groups for UI organization
    @Published private(set) var groups: [ChannelGroupInfo] = []

    // MARK: - Channel Introspection (Reserved)

    // Channel state visualization can be added via debugger service's GetSoundChipState
    // when needed. For now, channel names come from AudioFormat metadata.

    // MARK: - Connection State

    /// Whether format metadata has been loaded
    @Published private(set) var isLoaded: Bool = false

    /// Error message if connection failed
    @Published private(set) var errorMessage: String?

    // MARK: - Audio Pipeline Components

    /// Ring buffer shared with audio renderer
    let ringBuffer: AudioRingBuffer

    /// Audio renderer (DSP pipeline)
    let renderer: AudioRenderer

    /// Audio engine (AVAudioEngine wrapper)
    private var engine: AudioEngine?

    /// Serialises AudioEngine start/stop off the main thread. Instantiating the
    /// CoreAudio IO unit can block on a HAL mutex -- notably right after the host
    /// wakes from sleep, while CoreAudio is still coming back -- so it must never
    /// run on the main thread (the reconnect cascade calls connect there). A
    /// serial queue also keeps a start and a stop from overlapping across
    /// threads, which AVAudioEngine does not allow.
    private let engineQueue = DispatchQueue(label: "com.beebium.audio-engine")

    // MARK: - gRPC Client

    private var client: Beebium_AudioServiceNIOClient?
    private var streamTask: Task<Void, Never>?

    // MARK: - Initialization

    init() {
        ringBuffer = AudioRingBuffer()
        renderer = AudioRenderer(ringBuffer: ringBuffer)
    }

    // MARK: - Connection Lifecycle

    /// Connect to the server using an existing gRPC channel
    func connect(channel: GRPCChannel) {
        client = Beebium_AudioServiceNIOClient(channel: channel)

        // Retire an engine left by an earlier connect through the same queue.
        // AudioEngine's confinement contract is that stop() and deallocation
        // happen on engineQueue and never overlap a start; simply overwriting
        // self.engine would deallocate the old one here on the main actor.
        if let previous = engine {
            engineQueue.async { previous.stop() }
        }

        // Start the audio engine off the main thread (see engineQueue): its
        // CoreAudio setup can block, and must not freeze the UI.
        let engine = AudioEngine(renderer: renderer)
        self.engine = engine
        engineQueue.async { [weak self] in
            do {
                try engine.start()
            } catch {
                Task { @MainActor in
                    self?.errorMessage =
                        "Failed to start audio engine: \(error.localizedDescription)"
                }
            }
        }

        // Fetch format and start streaming
        streamTask = Task { [weak self] in
            await self?.fetchFormatAndSubscribe()
        }
    }

    /// Disconnect from the server
    func disconnect() {
        // Stop streaming
        streamTask?.cancel()
        streamTask = nil

        // Stop the engine on the same serial queue that starts it, so a start
        // still blocked in CoreAudio (e.g. post-wake) completes first and the
        // two never overlap across threads. The closure holds the last strong
        // reference until it runs.
        if let engine = engine {
            engineQueue.async { engine.stop() }
        }
        engine = nil

        // Clear state
        client = nil
        isLoaded = false
        sources = []
        groups = []
        errorMessage = nil

        // Reset ring buffer
        ringBuffer.reset()
    }

    // MARK: - Audio Format and Streaming

    private func fetchFormatAndSubscribe() async {
        guard let client = client else { return }

        // First, fetch audio format metadata
        do {
            let request = Beebium_GetAudioFormatRequest()
            let response = try await client.getAudioFormat(request).response.get()

            let newSources = response.sources.map { source in
                AudioSourceInfo(
                    id: source.sourceIndex,
                    name: source.sourceName,
                    channelNames: source.channelNames,
                    groupId: source.groupID
                )
            }

            let newGroups = response.groups.map { group in
                ChannelGroupInfo(
                    id: group.groupID,
                    name: group.groupName,
                    description: group.description_p,
                    color: group.color
                )
            }

            await MainActor.run {
                self.sampleRate = response.sampleRate
                self.sourceCount = response.sourceCount
                self.sources = newSources
                self.groups = newGroups
                self.isLoaded = true
                self.errorMessage = nil
            }
        } catch {
            await MainActor.run {
                self.errorMessage = "Failed to get audio format: \(error.localizedDescription)"
                self.isLoaded = false
            }
            return
        }

        // Then subscribe to audio stream
        var request = Beebium_SubscribeAudioRequest()
        request.chunkSize = 1024  // Samples per chunk

        let call = client.subscribeAudio(request) { [weak self] chunk in
            self?.handleAudioChunk(chunk)
        }

        // Wait for stream to complete or be cancelled
        do {
            _ = try await call.status.get()
        } catch {
            await MainActor.run {
                if self.isLoaded {
                    self.errorMessage = "Audio stream ended: \(error.localizedDescription)"
                }
            }
        }
    }

    private func handleAudioChunk(_ chunk: Beebium_AudioChunk) {
        // The samples field contains: sample_count * source_count * 4 bytes
        // Each sample has MAX_SOURCES (4) × 32-bit fields
        // We only need source 0 (SN76489) - extract it
        let data = chunk.samples
        let sampleCount = Int(chunk.sampleCount)
        let sourceCount = Int(sourceCount)  // Usually 4
        let bytesPerSample = sourceCount * 4  // 16 bytes per sample

        // Extract only source 0 from each sample
        var source0Data = Data(capacity: sampleCount * 4)
        data.withUnsafeBytes { rawBuffer in
            let bytes = rawBuffer.bindMemory(to: UInt8.self)
            for i in 0..<sampleCount {
                let offset = i * bytesPerSample
                // Source 0 is at the beginning of each sample (4 bytes)
                if offset + 4 <= bytes.count {
                    source0Data.append(bytes[offset])
                    source0Data.append(bytes[offset + 1])
                    source0Data.append(bytes[offset + 2])
                    source0Data.append(bytes[offset + 3])
                }
            }
        }

        _ = ringBuffer.write(data: source0Data)
    }

    // MARK: - Volume Control Passthrough

    /// Set master volume (0.0 to 1.0)
    func setMasterVolume(_ volume: Float) {
        renderer.setMasterVolume(volume)
    }

    /// Set per-channel volume (0.0 to 1.0)
    func setChannelVolume(_ channel: Int, volume: Float) {
        renderer.setChannelVolume(channel, volume: volume)
    }

    /// Set mute state
    func setMuted(_ muted: Bool) {
        renderer.setMuted(muted)
    }

    /// Set per-channel pan position (-1.0 to 1.0)
    func setPanPosition(_ channel: Int, pan: Float) {
        renderer.setPanPosition(channel, pan: pan)
    }

    // MARK: - Meter Access

    /// Get current meter state for a channel
    func getMeterState(channel: Int) -> AudioRenderer.MeterState {
        return renderer.getMeterState(channel: channel)
    }

    /// Get all channel meter states
    func getAllMeterStates() -> [AudioRenderer.MeterState] {
        return renderer.getAllMeterStates()
    }
}
