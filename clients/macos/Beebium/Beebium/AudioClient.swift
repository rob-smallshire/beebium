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

    /// The channels of every source in `groupId`, as (mixer channel index,
    /// channel name) pairs, ready for display in the mixer.
    ///
    /// A group may span several sources: the SN76489 is two sources (index 0 =
    /// "1","2", index 1 = "3","0") in one group. Each channel's mixer index comes
    /// from concatenating the group's sources in source-index order (0..3 =
    /// tone0, tone1, tone2, noise), which matches the order AudioRenderer unpacks
    /// and the indices AudioMixerState, meters, pan and mute-solo use. Each row
    /// keeps its mixer channel index paired with its name whatever its display
    /// position, so it controls and meters that channel (the row labelled "0" is
    /// mixer channel 3, noise).
    ///
    /// Rows are then displayed ordered by channel name compared numerically when
    /// every name in the group parses as an integer (so "10" follows "2"); if any
    /// name is non-numeric the source order is kept. The (index, name) pairing is
    /// never changed, only the row order: for the SN76489 this displays the noise
    /// channel ("0", mixer index 3) first, then tones "1","2","3".
    static func channelsInGroup(_ sources: [AudioSourceInfo],
                                groupId: UInt32) -> [(Int, String)] {
        let channels = sources
            .filter { $0.groupId == groupId }
            .sorted { $0.id < $1.id }
            .flatMap { $0.channelNames }
            .enumerated()
            .map { ($0.offset, $0.element) }

        let numbers = channels.compactMap { Int($0.1) }
        guard numbers.count == channels.count else { return channels }

        // Stable numeric sort: equal numbers keep their source (mixer-index) order.
        return channels
            .sorted { lhs, rhs in
                let a = Int(lhs.1)!, b = Int(rhs.1)!
                return a != b ? a < b : lhs.0 < rhs.0
            }
    }

    /// The group ids the mixer shows: those owning a source on the fields the
    /// renderer's per-channel sliders and pan actually drive -- the SN76489, on
    /// source fields 0 and 1. A source on a later field (a Music 5000 on field 2,
    /// a Music 3000 on field 3) has no per-channel client control -- the master
    /// volume governs it -- so its group is not shown, rather than shipping dead
    /// sliders. The client does not know what those sources are; it hides them
    /// generically by field. `snFieldCount` is the number of leading source
    /// fields the SN path owns (2). Pure.
    static func shownGroupIDs(sources: [AudioSourceInfo], snFieldCount: Int) -> Set<UInt32> {
        Set(sources.filter { $0.id < UInt32(snFieldCount) }.map { $0.groupId })
    }
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

    /// Ring buffer shared with audio renderer: the SN76489's two source fields
    /// (fields 0 and 1), one UInt64 frame each.
    let ringBuffer: AudioRingBuffer

    /// Parallel ring buffer for the extension fields past the SN76489's two
    /// (field 2 in the low half, field 3 in the high half of each UInt64 frame:
    /// a Music 5000 and a Music 3000). Written and read in lockstep with
    /// `ringBuffer` -- same capacity, identical counts -- so a frame lines up in
    /// both. Empty for a server with only the SN76489.
    let extRingBuffer: AudioRingBuffer

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
        // Size the ring buffer to the pre-roll's 500 ms envelope so the adaptive
        // target has room to climb to 480 ms under a starving host (#126). The
        // steady-state latency stays at the pre-roll's 120 ms floor.
        let capacity = AudioPreRoll.capacityFrames(sampleRate: 48000)
        ringBuffer = AudioRingBuffer(capacity: capacity)
        extRingBuffer = AudioRingBuffer(capacity: capacity)
        renderer = AudioRenderer(ringBuffer: ringBuffer, extRingBuffer: extRingBuffer)
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

        // Reset ring buffers (both, so they stay in lockstep next session)
        ringBuffer.reset()
        extRingBuffer.reset()
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

            // Which extension fields past the SN76489's two are bipolar 2x16
            // signed stereo sources the renderer should mix: field 2 is slot 0 (a
            // Music 5000), field 3 slot 1 (a Music 3000). The server is the single
            // source of truth -- the client does not know what a Music 5000 is,
            // only that a field is a bipolar stereo source.
            func isBipolarStereo(fieldIndex: UInt32) -> Bool {
                response.sources.contains {
                    $0.sourceIndex == fieldIndex
                        && $0.encoding == .encoding2X16BitSigned
                        && $0.bipolar
                }
            }
            let field2Bipolar = isBipolarStereo(fieldIndex: 2)
            let field3Bipolar = isBipolarStereo(fieldIndex: 3)

            await MainActor.run {
                self.sampleRate = response.sampleRate
                self.sourceCount = response.sourceCount
                self.sources = newSources
                self.groups = newGroups
                self.renderer.setExtensionBipolarSlots(field2: field2Bipolar, field3: field3Bipolar)
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
        // samples = sample_count * source_count * 4 bytes, each sample N 32-bit
        // fields. Fields 0 and 1 are the SN76489 (source 0 = tone0|tone1, source
        // 1 = tone2|noise), packed low half first into one UInt64 for ringBuffer.
        // Fields 2 and 3, when the server has them, are the extension sources
        // (field 2 low half, field 3 high half of one UInt64) for extRingBuffer,
        // written in lockstep. The renderer, told by GetAudioFormat which fields
        // are bipolar, mixes them. Fields past 3 are not consumed by any source
        // the protocol defines today.
        let data = chunk.samples
        let sampleCount = Int(chunk.sampleCount)
        let bytesPerSample = Int(sourceCount) * 4
        if sampleCount == 0 { return }

        let haveExt = bytesPerSample >= 12
        var snFrames = [UInt64](repeating: 0, count: sampleCount)
        var extFrames = haveExt ? [UInt64](repeating: 0, count: sampleCount) : []

        data.withUnsafeBytes { raw in
            let count = raw.count
            for i in 0..<sampleCount {
                let base = i * bytesPerSample
                if base + 8 <= count {
                    snFrames[i] = raw.loadUnaligned(fromByteOffset: base, as: UInt64.self)
                }
                if haveExt {
                    var ext: UInt64 = 0
                    if base + 12 <= count {
                        ext = UInt64(raw.loadUnaligned(fromByteOffset: base + 8, as: UInt32.self))
                    }
                    if base + 16 <= count {
                        ext |= UInt64(raw.loadUnaligned(fromByteOffset: base + 12, as: UInt32.self)) << 32
                    }
                    extFrames[i] = ext
                }
            }
        }

        // Write the SN frames, then the same count of extension frames, so the
        // two buffers advance together and a frame lines up in both.
        _ = snFrames.withUnsafeBufferPointer { ringBuffer.write($0.baseAddress!, count: sampleCount) }
        if haveExt {
            _ = extFrames.withUnsafeBufferPointer { extRingBuffer.write($0.baseAddress!, count: sampleCount) }
        }
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

    // MARK: - Buffer Health

    /// Playback ring-buffer health for the current session: samples the producer
    /// dropped because the buffer was full (overflow), and the frames of silence
    /// substituted for underruns with the number of underrun events. Both are
    /// zero on a machine that keeps up; non-zero means the host starved the audio
    /// threads (see AudioPreRoll for how underruns are then absorbed).
    struct BufferHealth: Equatable, Sendable {
        var droppedSamples: UInt64 = 0
        var underrunSamples: UInt64 = 0
        var underrunEvents: UInt64 = 0
    }

    /// Read the current playback ring-buffer health.
    func bufferHealth() -> BufferHealth {
        BufferHealth(
            droppedSamples: ringBuffer.totalSamplesDropped,
            underrunSamples: ringBuffer.totalUnderrunSamples,
            underrunEvents: ringBuffer.totalUnderrunEvents)
    }
}
