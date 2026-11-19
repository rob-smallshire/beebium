// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

import AVFAudio
import CoreAudio
import Foundation
import GRPC

/// Captures a chosen host input device through AVAudioEngine and streams it to
/// the cassette deck as the live tape, via CassetteService.StreamAudio.
///
/// No voice processing (MANDATORY, section 9.0): the plain input node is used
/// and `setVoiceProcessingEnabled` is never called, so there is no automatic
/// gain, noise suppression or echo cancellation on the captured line; a surprise
/// default is refused rather than streamed.
///
/// Sendability is unchecked because the safety rests on a confinement contract
/// the compiler cannot see: `start`, `stop` and `cancel` run on the owner's main
/// actor, and the capture tap -- on a real-time audio thread -- reads only
/// immutable configuration and the tap-confined `Chunker`, and sends on the gRPC
/// call, which is itself safe to use from any thread.
final class LiveAudioCapture: @unchecked Sendable {

    /// A client-side failure to begin capturing, before the stream opens. A
    /// sentence, as the deck's other errors are; a server refusal of the stream
    /// comes back later in the summary instead.
    enum CaptureError: LocalizedError {
        case deviceUnavailable
        case unsupportedFormat
        case engineFailed(String)

        var errorDescription: String? {
            switch self {
            case .deviceUnavailable:
                return "That audio input device could not be opened."
            case .unsupportedFormat:
                return "That audio input device's format is not one the deck accepts."
            case .engineFailed(let message):
                return message
            }
        }
    }

    private let engine = AVAudioEngine()
    private var call: ClientStreamingCall<Beebium_AudioInputChunk, Beebium_StreamAudioSummary>?
    private var chunker: LiveAudioInput.Chunker?
    /// Reads each captured chunk's peak and clip run, carrying the open
    /// full-scale run across chunk boundaries so a straddling clip is one run.
    /// Written only by the capture tap, so it is confined like `chunker`.
    private var clipRunMeter = LiveAudioInput.ClipRunMeter()
    private var started = false

    /// The sample rate the stream was opened with, known once `start` has read
    /// the capture format; zero before that. The deck reads it for the
    /// made-up-silence figure in a starve message.
    private(set) var streamSampleRate: UInt32 = 0

    /// The format `StreamAudio` was opened with, so a later configuration change
    /// can be compared against it. Nil before `start`.
    private(set) var streamedFormat: Beebium_AudioInputFormat?

    /// The chosen device's CoreAudio id, so a configuration change can tell a
    /// format change (the device is still listed) from a disconnection (it is
    /// no longer in the host's inputs).
    private var deviceID: AudioDeviceID = 0

    /// Called on the main actor's behalf (the owner hops) when a configuration
    /// change ends the stream: the device changed format or was disconnected
    /// under the open stream (cassette round 12).
    private var onDeviceChange: ((LiveAudioInput.DeviceChangeFailure) -> Void)?

    /// The observer of the engine's configuration-change notification, removed
    /// when the stream ends.
    private var configObserver: NSObjectProtocol?

    /// Begin capturing `device` and streaming it as the live tape. `onSummary`
    /// is called once when the stream ends: with the server's summary after a
    /// graceful half-close (`stop`), or a failure on `cancel` or connection
    /// loss. The owner releases this capture in that callback.
    /// `onPeak` is called for every captured chunk with its peak level and clip
    /// flag, on the real-time audio thread; the owner hops it to the main actor
    /// to drive the level meter.
    func start(device: LiveAudioDevice,
               client: Beebium_CassetteServiceNIOClient,
               onPeak: @escaping (LiveAudioInput.Peak) -> Void = { _ in },
               onDeviceChange: @escaping (LiveAudioInput.DeviceChangeFailure) -> Void = { _ in },
               onSummary: @escaping (Result<Beebium_StreamAudioSummary, Error>) -> Void) throws {
        let inputNode = engine.inputNode

        // Point the engine's input at the chosen device by its CoreAudio id,
        // while the engine is stopped and before reading the capture format.
        guard let unit = inputNode.audioUnit else { throw CaptureError.deviceUnavailable }
        var deviceID = device.id
        let status = AudioUnitSetProperty(
            unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0,
            &deviceID, UInt32(MemoryLayout<AudioDeviceID>.size))
        guard status == noErr else { throw CaptureError.deviceUnavailable }

        // MANDATORY: never enable voice processing. The default input node has
        // it off; refuse to capture if anything has turned it on.
        if inputNode.isVoiceProcessingEnabled {
            NSLog("[LiveAudioCapture] Voice processing is enabled on the input node; refusing")
            throw CaptureError.unsupportedFormat
        }

        let tapFormat = inputNode.outputFormat(forBus: 0)
        guard let format = LiveAudioInput.inputFormat(from: tapFormat, deviceName: device.name) else {
            throw CaptureError.unsupportedFormat
        }
        streamSampleRate = format.sampleRate
        streamedFormat = format
        deviceID = device.id
        self.onDeviceChange = onDeviceChange
        let frameBytes = LiveAudioInput.frameBytes(for: format)
        let framesPerChunk = LiveAudioInput.framesPerChunk(sampleRate: format.sampleRate)
        chunker = LiveAudioInput.Chunker(chunkBytes: framesPerChunk * frameBytes)

        let call = client.streamAudio()
        self.call = call
        // The format is the first message; the server opens the live tape on it.
        call.sendMessage(.with { $0.format = format }, promise: nil)
        call.response.whenComplete { result in onSummary(result.map { $0 }) }

        let channels = Int(format.channels)
        let sampleFormat = format.sampleFormat
        inputNode.installTap(onBus: 0, bufferSize: AVAudioFrameCount(framesPerChunk),
                             format: tapFormat) { [weak self] buffer, _ in
            guard let self, let call = self.call else { return }
            let bytes = LiveAudioInput.samples(from: buffer, channels: channels,
                                               sampleFormat: sampleFormat)
            // The level meter is read from exactly the bytes streamed, taking
            // the left channel, so what it shows is what the server receives. The
            // meter carries the open clip run across chunks, so a clip straddling
            // a chunk boundary counts as one run. The tap is the only writer.
            onPeak(self.clipRunMeter.peak(ofInterleaved: bytes, sampleFormat: sampleFormat,
                                          channels: channels))
            // The tap is the only writer of the chunker, so this is confined.
            for chunk in self.chunker?.take(bytes) ?? [] {
                call.sendMessage(.with { $0.samples = chunk }, promise: nil)
            }
        }

        do {
            try engine.start()
        } catch {
            inputNode.removeTap(onBus: 0)
            self.call = nil
            call.cancel(promise: nil)
            throw CaptureError.engineFailed(error.localizedDescription)
        }
        started = true

        // Watch for the chosen device changing format or going away under the
        // open stream (cassette round 12). The notification can arrive on any
        // thread; the decision is a pure function and the owner hops the result
        // to the main actor.
        configObserver = NotificationCenter.default.addObserver(
            forName: .AVAudioEngineConfigurationChange, object: engine, queue: nil
        ) { [weak self] _ in
            self?.handleConfigurationChange()
        }
    }

    /// Read the input node's current format after a configuration change and,
    /// if it no longer matches the streamed format (or the device has gone),
    /// report the failure so the owner can end the stream.
    private func handleConfigurationChange() {
        guard started, let streamed = streamedFormat else { return }
        let node = engine.inputNode
        let current = node.outputFormat(forBus: 0)
        // A format reporting no channels reads as the device having gone.
        let effective: AVAudioFormat? = current.channelCount > 0 ? current : nil
        let listed = LiveAudioDevices.inputs().contains { $0.id == deviceID }
        guard let failure = LiveAudioInput.deviceChange(
            streamed: streamed, current: effective, deviceListed: listed) else { return }
        onDeviceChange?(failure)
    }

    /// Stop watching for configuration changes, if we were.
    private func removeConfigObserver() {
        if let configObserver {
            NotificationCenter.default.removeObserver(configObserver)
            self.configObserver = nil
        }
    }

    /// Stop capturing and half-close the stream: the server plays out what its
    /// ring holds and then ejects the live tape (section 9.0). The summary
    /// arrives through the `start` completion.
    func stop() {
        guard started else { return }
        started = false
        removeConfigObserver()
        engine.inputNode.removeTap(onBus: 0)
        engine.stop()
        // The tap has stopped, so draining the remainder is safe here.
        if let remainder = chunker?.drain(), !remainder.isEmpty {
            call?.sendMessage(.with { $0.samples = remainder }, promise: nil)
        }
        call?.sendEnd(promise: nil)
    }

    /// Stop capturing and abandon the stream at once, without a graceful
    /// half-close -- for a teardown where the channel is going anyway
    /// (disconnect).
    func cancel() {
        guard started else { return }
        started = false
        removeConfigObserver()
        engine.inputNode.removeTap(onBus: 0)
        engine.stop()
        call?.cancel(promise: nil)
        call = nil
    }

    deinit {
        if started {
            removeConfigObserver()
            engine.inputNode.removeTap(onBus: 0)
            engine.stop()
            call?.cancel(promise: nil)
        }
    }
}
