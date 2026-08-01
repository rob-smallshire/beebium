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

import AVFAudio
import Atomics
import Foundation

/// AVAudioEngine wrapper for audio playback.
///
/// Uses AVAudioSourceNode for push-based audio rendering with minimal latency.
/// The render callback is called from a high-priority audio thread.
///
/// Sendability is unchecked because the safety comes from a confinement
/// contract the compiler cannot see, not from the types involved:
///
///  * `start()`, `stop()`, `pause()` and `resume()` -- and therefore every
///    mutation of `sourceNode` and `isRunning` -- run only on `AudioClient`'s
///    serial `engineQueue`. AVAudioEngine does not tolerate an overlapping
///    start and stop, so that queue is load-bearing regardless of Swift
///    concurrency; the owner must not call these from anywhere else.
///  * `deinit` calls `stop()`. `AudioClient` hands the last strong reference
///    to a block on `engineQueue`, so deallocation happens there too.
///  * `renderCallback` runs on the audio thread and touches only the
///    renderer (itself `@unchecked Sendable`, atomics inside) and the
///    underrun counter, which is atomic for that reason.
final class AudioEngine: @unchecked Sendable {

    // MARK: - Audio Engine Components

    private let engine: AVAudioEngine
    private var sourceNode: AVAudioSourceNode?
    private let audioFormat: AVAudioFormat

    // MARK: - Renderer

    private let renderer: AudioRenderer

    // MARK: - State

    private(set) var isRunning: Bool = false

    /// Sample rate used by the engine
    let sampleRate: Double

    // MARK: - Statistics

    /// Incremented on the audio thread, read from wherever the statistics are
    /// wanted, so it is atomic rather than a plain `Int`.
    private let underrunCount = ManagedAtomic<Int>(0)

    // MARK: - Initialization

    /// Create an audio engine with the specified renderer and sample rate.
    ///
    /// - Parameters:
    ///   - renderer: The audio renderer that provides samples
    ///   - sampleRate: Sample rate in Hz (default: 48000)
    init(renderer: AudioRenderer, sampleRate: Double = 48000.0) {
        self.renderer = renderer
        self.sampleRate = sampleRate
        self.engine = AVAudioEngine()

        // Create stereo float format
        guard let format = AVAudioFormat(
            commonFormat: .pcmFormatFloat32,
            sampleRate: sampleRate,
            channels: 2,
            interleaved: false
        ) else {
            fatalError("Failed to create audio format")
        }
        self.audioFormat = format
    }

    deinit {
        stop()
    }

    // MARK: - Lifecycle

    /// Start audio playback.
    ///
    /// Creates the source node, connects it to the output, and starts the engine.
    func start() throws {
        guard !isRunning else { return }

        // Create source node with render callback
        let node = AVAudioSourceNode(format: audioFormat) { [weak self] isSilence, timestamp, frameCount, outputData in
            guard let self = self else {
                isSilence.pointee = true
                return noErr
            }
            return self.renderCallback(
                isSilence: isSilence,
                timestamp: timestamp,
                frameCount: frameCount,
                outputData: outputData
            )
        }
        sourceNode = node

        // Attach and connect source node to main mixer
        engine.attach(node)
        engine.connect(node, to: engine.mainMixerNode, format: audioFormat)

        // Start the engine
        try engine.start()
        isRunning = true

        // Reset filters on start to clear any stale state
        renderer.resetFilters()
    }

    /// Stop audio playback.
    func stop() {
        guard isRunning else { return }

        engine.stop()

        if let node = sourceNode {
            engine.disconnectNodeOutput(node)
            engine.detach(node)
            sourceNode = nil
        }

        isRunning = false
    }

    /// Pause audio playback (keeps engine configured).
    func pause() {
        engine.pause()
    }

    /// Resume paused playback.
    func resume() throws {
        try engine.start()
    }

    // MARK: - Render Callback

    private func renderCallback(
        isSilence: UnsafeMutablePointer<ObjCBool>,
        timestamp: UnsafePointer<AudioTimeStamp>,
        frameCount: AVAudioFrameCount,
        outputData: UnsafeMutablePointer<AudioBufferList>
    ) -> OSStatus {
        let ablPointer = UnsafeMutableAudioBufferListPointer(outputData)

        // Get pointers to left and right channel buffers
        guard ablPointer.count >= 2,
              let leftData = ablPointer[0].mData,
              let rightData = ablPointer[1].mData else {
            isSilence.pointee = true
            return noErr
        }

        let leftBuffer = leftData.assumingMemoryBound(to: Float.self)
        let rightBuffer = rightData.assumingMemoryBound(to: Float.self)

        // Call renderer to fill buffers
        let samplesRendered = renderer.render(
            frameCount: Int(frameCount),
            leftBuffer: leftBuffer,
            rightBuffer: rightBuffer
        )

        // Fill remaining samples with silence if underrun
        if samplesRendered < Int(frameCount) {
            underrunCount.wrappingIncrement(ordering: .relaxed)
            for i in samplesRendered..<Int(frameCount) {
                leftBuffer[i] = 0
                rightBuffer[i] = 0
            }
        }

        // Report silence if no samples rendered
        isSilence.pointee = ObjCBool(samplesRendered == 0)

        return noErr
    }

    // MARK: - Statistics

    /// Number of buffer underruns since engine started
    var bufferUnderruns: Int {
        return underrunCount.load(ordering: .relaxed)
    }

    /// Reset underrun counter
    func resetUnderrunCount() {
        underrunCount.store(0, ordering: .relaxed)
    }
}
