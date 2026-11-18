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

import AudioToolbox
import Atomics
import Foundation

/// Output-only audio playback, built on Apple's Default Output audio unit.
///
/// This used to be an `AVAudioEngine`, but on macOS 14 an `AVAudioEngine`
/// realises its input node (its I/O audio unit is created with the input bus
/// enabled) as soon as the graph is started -- even for output-only playback,
/// and even when the source is not routed through the main mixer. That
/// registers microphone intent, so a signed app carrying a usage string asks
/// the user for the microphone the moment a machine window's audio starts,
/// which is wrong: nothing here reads the input.
///
/// The Default Output unit (`kAudioUnitType_Output` /
/// `kAudioUnitSubType_DefaultOutput`) has an output element only -- no input
/// from any device -- so it cannot open the microphone. We feed it with a
/// render callback, exactly as the `AVAudioSourceNode` did.
///
/// Sendability is unchecked because the safety comes from a confinement
/// contract the compiler cannot see, not from the types involved:
///
///  * `start()`, `stop()`, `pause()` and `resume()` -- and therefore every
///    mutation of `outputUnit` and `isRunning` -- run only on `AudioClient`'s
///    serial `engineQueue`; the owner must not call these from anywhere else.
///  * `deinit` calls `stop()`. `AudioClient` hands the last strong reference
///    to a block on `engineQueue`, so deallocation happens there too.
///  * `render(...)` runs on the audio thread and touches only the renderer
///    (itself `@unchecked Sendable`, atomics inside) and the underrun counter,
///    which is atomic for that reason.
final class AudioEngine: @unchecked Sendable {

    /// A failure to build or start the output unit, as a Core Audio status.
    enum AudioEngineError: Error {
        case outputUnitUnavailable
        case coreAudio(String, OSStatus)
    }

    // MARK: - Output unit

    private var outputUnit: AudioUnit?

    // MARK: - Renderer

    private let renderer: AudioRenderer

    // MARK: - State

    private(set) var isRunning: Bool = false

    /// Sample rate used by the engine.
    let sampleRate: Double

    // MARK: - Statistics

    /// Incremented on the audio thread, read from wherever the statistics are
    /// wanted, so it is atomic rather than a plain `Int`.
    private let underrunCount = ManagedAtomic<Int>(0)

    // MARK: - Initialization

    /// Create an audio engine with the specified renderer and sample rate.
    init(renderer: AudioRenderer, sampleRate: Double = 48000.0) {
        self.renderer = renderer
        self.sampleRate = sampleRate
    }

    deinit {
        stop()
    }

    // MARK: - Audio unit description and format

    /// Apple's Default Output audio unit. It has an output element only and
    /// never opens an input device, so -- unlike `AVAudioEngine` -- it cannot
    /// touch the microphone.
    static var outputComponentDescription: AudioComponentDescription {
        AudioComponentDescription(
            componentType: kAudioUnitType_Output,
            componentSubType: kAudioUnitSubType_DefaultOutput,
            componentManufacturer: kAudioUnitManufacturer_Apple,
            componentFlags: 0,
            componentFlagsMask: 0)
    }

    /// The format we hand the output unit: non-interleaved 32-bit float stereo
    /// at our sample rate. The unit converts it to the hardware's format.
    static func streamFormat(sampleRate: Double) -> AudioStreamBasicDescription {
        let bytesPerSample = UInt32(MemoryLayout<Float>.size)
        return AudioStreamBasicDescription(
            mSampleRate: sampleRate,
            mFormatID: kAudioFormatLinearPCM,
            mFormatFlags: kAudioFormatFlagIsFloat
                | kAudioFormatFlagIsNonInterleaved
                | kAudioFormatFlagIsPacked,
            mBytesPerPacket: bytesPerSample,
            mFramesPerPacket: 1,
            mBytesPerFrame: bytesPerSample,
            mChannelsPerFrame: 2,
            mBitsPerChannel: 32,
            mReserved: 0)
    }

    // MARK: - Lifecycle

    /// Start audio playback: instantiate the Default Output unit, set our format
    /// and render callback, and start it.
    func start() throws {
        guard !isRunning else { return }

        var description = Self.outputComponentDescription
        guard let component = AudioComponentFindNext(nil, &description) else {
            throw AudioEngineError.outputUnitUnavailable
        }

        var unit: AudioUnit?
        try check("instantiate output unit", AudioComponentInstanceNew(component, &unit))
        guard let unit else { throw AudioEngineError.outputUnitUnavailable }
        outputUnit = unit

        do {
            var format = Self.streamFormat(sampleRate: sampleRate)
            try check("set stream format", AudioUnitSetProperty(
                unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0,
                &format, UInt32(MemoryLayout<AudioStreamBasicDescription>.size)))

            var callback = AURenderCallbackStruct(
                inputProc: audioEngineRenderCallback,
                inputProcRefCon: Unmanaged.passUnretained(self).toOpaque())
            try check("set render callback", AudioUnitSetProperty(
                unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0,
                &callback, UInt32(MemoryLayout<AURenderCallbackStruct>.size)))

            try check("initialize output unit", AudioUnitInitialize(unit))
            try check("start output unit", AudioOutputUnitStart(unit))
        } catch {
            // Undo a partial start so a later retry begins clean.
            AudioUnitUninitialize(unit)
            AudioComponentInstanceDispose(unit)
            outputUnit = nil
            throw error
        }

        isRunning = true

        // Reset filters and the playback pre-roll on start to clear any stale
        // state, so buffered latency does not carry over from a previous session.
        renderer.resetFilters()
        renderer.resetPreRoll()
    }

    /// Stop audio playback and dispose the output unit.
    func stop() {
        guard let unit = outputUnit else { return }
        AudioOutputUnitStop(unit)
        AudioUnitUninitialize(unit)
        AudioComponentInstanceDispose(unit)
        outputUnit = nil
        isRunning = false
    }

    /// Pause audio playback (keeps the unit allocated).
    func pause() {
        if let unit = outputUnit {
            AudioOutputUnitStop(unit)
        }
    }

    /// Resume paused playback.
    func resume() throws {
        if let unit = outputUnit {
            try check("resume output unit", AudioOutputUnitStart(unit))
        }
    }

    private func check(_ what: String, _ status: OSStatus) throws {
        guard status == noErr else { throw AudioEngineError.coreAudio(what, status) }
    }

    // MARK: - Render Callback

    /// Fill the output unit's non-interleaved stereo buffers from the renderer.
    /// Runs on the audio thread.
    fileprivate func render(frameCount: UInt32,
                            into ioData: UnsafeMutablePointer<AudioBufferList>) -> OSStatus {
        let buffers = UnsafeMutableAudioBufferListPointer(ioData)

        guard buffers.count >= 2,
              let leftData = buffers[0].mData,
              let rightData = buffers[1].mData else {
            return noErr
        }

        let leftBuffer = leftData.assumingMemoryBound(to: Float.self)
        let rightBuffer = rightData.assumingMemoryBound(to: Float.self)

        let samplesRendered = renderer.render(
            frameCount: Int(frameCount),
            leftBuffer: leftBuffer,
            rightBuffer: rightBuffer)

        // Fill remaining samples with silence if underrun.
        if samplesRendered < Int(frameCount) {
            underrunCount.wrappingIncrement(ordering: .relaxed)
            for i in samplesRendered..<Int(frameCount) {
                leftBuffer[i] = 0
                rightBuffer[i] = 0
            }
        }

        return noErr
    }

    // MARK: - Statistics

    /// Number of buffer underruns since engine started.
    var bufferUnderruns: Int {
        return underrunCount.load(ordering: .relaxed)
    }

    /// Reset underrun counter.
    func resetUnderrunCount() {
        underrunCount.store(0, ordering: .relaxed)
    }
}

/// The Core Audio render callback: hand off to the `AudioEngine` carried in the
/// ref-con. A top-level function so it is a plain C function pointer with no
/// Swift context captured.
private func audioEngineRenderCallback(
    inRefCon: UnsafeMutableRawPointer,
    ioActionFlags: UnsafeMutablePointer<AudioUnitRenderActionFlags>,
    inTimeStamp: UnsafePointer<AudioTimeStamp>,
    inBusNumber: UInt32,
    inNumberFrames: UInt32,
    ioData: UnsafeMutablePointer<AudioBufferList>?
) -> OSStatus {
    guard let ioData else { return noErr }
    let engine = Unmanaged<AudioEngine>.fromOpaque(inRefCon).takeUnretainedValue()
    return engine.render(frameCount: inNumberFrames, into: ioData)
}
