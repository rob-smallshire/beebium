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
import Foundation

/// The pure pieces of streaming a host audio input to the deck as the live
/// tape: the format StreamAudio is opened with (built from what the capture tap
/// delivers), the interleaving of a capture buffer into the little-endian bytes
/// the server decodes, the re-chunking into the 10 to 20 ms chunks the server
/// expects, and the run-time gate on the microphone usage string. No audio
/// engine or gRPC here, so each is tested without hardware or a server.
enum LiveAudioInput {
    /// The target chunk length, within the 10 to 20 ms the server wants. At a
    /// typical 48 kHz a chunk is then 720 frames, a comfortable parcel for the
    /// stream without adding meaningful latency over the ring's own quarter
    /// second.
    static let chunkSeconds = 0.015

    // MARK: - Device format to stream format

    /// The `AudioInputFormat` to open `StreamAudio` with, from the format the
    /// capture tap will actually deliver (the input node's processing format)
    /// and the host's name for the device, which names the live tape.
    ///
    /// Returns nil for a format the server does not accept: a rate outside 8000
    /// to 192000 Hz, a sample format that is neither signed 16-bit nor 32-bit
    /// float, or no channel. A device offering more than two input channels
    /// contributes its first two, with the left taken by default.
    static func inputFormat(from format: AVAudioFormat,
                            deviceName: String) -> Beebium_AudioInputFormat? {
        let rate = Int(format.sampleRate.rounded())
        guard rate >= 8000, rate <= 192000 else { return nil }

        let sampleFormat: Beebium_AudioSampleFormat
        switch format.commonFormat {
        case .pcmFormatInt16: sampleFormat = .s16
        case .pcmFormatFloat32: sampleFormat = .f32
        default: return nil
        }

        let available = Int(format.channelCount)
        guard available >= 1 else { return nil }
        let channels = min(available, 2)

        var result = Beebium_AudioInputFormat()
        result.sampleRate = UInt32(rate)
        result.sampleFormat = sampleFormat
        result.channels = UInt32(channels)
        // The channel field is only meaningful for a stereo stream; the server
        // reads "left" (absent) or "right".
        result.channel = channels == 2 ? "left" : ""
        result.deviceName = deviceName
        return result
    }

    /// Bytes per interleaved frame of a stream format: one sample per channel.
    static func frameBytes(for format: Beebium_AudioInputFormat) -> Int {
        let sampleBytes = format.sampleFormat == .s16 ? 2 : 4
        return max(1, Int(format.channels)) * sampleBytes
    }

    /// The whole frames to a chunk at `chunkSeconds` for a sample rate, at
    /// least one. Frame-aligned by construction, so a chunk never splits a
    /// frame.
    static func framesPerChunk(sampleRate: UInt32) -> Int {
        max(1, Int((Double(sampleRate) * chunkSeconds).rounded()))
    }

    // MARK: - Capture buffer to stream bytes

    /// Interleave the first `channels` planar float channels of a capture buffer
    /// into the little-endian bytes the server decodes -- frame by frame, each
    /// channel's sample in turn -- in the stream's sample format. The tap always
    /// delivers float, so an `.s16` stream is encoded from it here.
    static func samples(from buffer: AVAudioPCMBuffer,
                        channels: Int,
                        sampleFormat: Beebium_AudioSampleFormat) -> Data {
        guard let planar = buffer.floatChannelData else { return Data() }
        let frames = Int(buffer.frameLength)
        let available = Int(buffer.format.channelCount)
        let used = max(1, min(channels, available))
        var channelSamples: [[Float]] = []
        channelSamples.reserveCapacity(used)
        for channel in 0..<used {
            channelSamples.append(Array(UnsafeBufferPointer(start: planar[channel], count: frames)))
        }
        return interleavedLittleEndian(channels: channelSamples, sampleFormat: sampleFormat)
    }

    /// The pure core of `samples(from:...)`: interleave planar float channels of
    /// equal length into little-endian bytes. Frames are taken up to the
    /// shortest channel. A float is written as its 32-bit pattern for `.f32`, or
    /// clamped to +/-1 and scaled to signed 16-bit for `.s16`.
    static func interleavedLittleEndian(channels: [[Float]],
                                        sampleFormat: Beebium_AudioSampleFormat) -> Data {
        guard let frames = channels.map(\.count).min(), frames > 0, !channels.isEmpty else {
            return Data()
        }
        let sampleBytes = sampleFormat == .s16 ? 2 : 4
        var data = Data(capacity: frames * channels.count * sampleBytes)
        for frame in 0..<frames {
            for channel in channels {
                switch sampleFormat {
                case .s16:
                    let clamped = max(-1.0, min(1.0, channel[frame]))
                    let value = Int16((clamped * 32767.0).rounded())
                    withUnsafeBytes(of: UInt16(bitPattern: value).littleEndian) { data.append(contentsOf: $0) }
                default:
                    withUnsafeBytes(of: channel[frame].bitPattern.littleEndian) { data.append(contentsOf: $0) }
                }
            }
        }
        return data
    }

    // MARK: - Level metering

    /// The peak level of one capture chunk and its longest run of full-scale
    /// samples, for the live row's level meter.
    struct Peak: Equatable {
        /// The largest absolute sample in the chunk, normalised to full scale
        /// (0 silent .. 1 full scale).
        var level: Float
        /// The longest run of consecutive full-scale samples in the taken
        /// channel. The meter lights its clip mark only on a sustained run
        /// (`LevelMeterState.clipRunSamples` or more), so a single-sample click
        /// at a splice or the head of a tape is not read as clipping (round 12).
        var clipRun: Int
        /// Whether any taken sample reached the converter's full scale (a run of
        /// at least one). Kept for callers that only ask whether a rail was hit.
        var clipped: Bool { clipRun > 0 }

        static let silent = Peak(level: 0, clipRun: 0)
    }

    /// The peak and clip run of a single chunk, read from the same little-endian
    /// interleaved bytes the deck streams (`samples(from:)`), in the stream's
    /// sample format. Only the left channel of a stereo chunk is read: clipping
    /// happens at the host's analogue-to-digital converter, which is the line
    /// the server reads, so a full-scale sample in the unread right channel must
    /// not light the mark. An S16 sample is normalised by 32768 (so the negative
    /// rail reads as full scale) and clips at either rail; an F32 sample clips at
    /// a magnitude of 1 or more. The clip run is the longest span of consecutive
    /// full-scale samples, so the meter can hold for sustained clipping and
    /// ignore an isolated click. Pure, so it is tested without hardware.
    ///
    /// This reads the chunk on its own, with no run carried in. Successive
    /// capture chunks must be read through `ClipRunMeter`, which carries the open
    /// run across the boundary so a clip straddling two chunks is counted as one
    /// run rather than two short ones.
    static func peak(ofInterleaved data: Data,
                     sampleFormat: Beebium_AudioSampleFormat,
                     channels: Int) -> Peak {
        let scanned = scan(ofInterleaved: data, sampleFormat: sampleFormat,
                           channels: channels, carryRun: 0)
        return Peak(level: scanned.level, clipRun: scanned.clipRun)
    }

    /// The peak level, the longest clip run, and the full-scale run still open at
    /// the end of the chunk, reading the taken (left) channel. `carryRun` is the
    /// length of a full-scale run still open at the end of the previous chunk, so
    /// a run spanning the boundary is one run: the first sample of this chunk
    /// extends it. `openRun` carries it on to the chunk after. The clip run is
    /// the longest span that includes at least one sample of this chunk, so a run
    /// wholly in the previous chunk is not re-reported here.
    private static func scan(ofInterleaved data: Data,
                             sampleFormat: Beebium_AudioSampleFormat,
                             channels: Int,
                             carryRun: Int) -> (level: Float, clipRun: Int, openRun: Int) {
        let channelCount = max(1, channels)
        var level: Float = 0
        var clipRun = 0
        var currentRun = max(0, carryRun)
        data.withUnsafeBytes { raw in
            switch sampleFormat {
            case .s16:
                let frameBytes = channelCount * 2
                var offset = 0
                while offset + 2 <= raw.count {
                    let sample = Int16(littleEndian:
                        raw.loadUnaligned(fromByteOffset: offset, as: Int16.self))
                    if sample == Int16.max || sample == Int16.min {
                        currentRun += 1
                        if currentRun > clipRun { clipRun = currentRun }
                    } else {
                        currentRun = 0
                    }
                    // abs(Int16.min) overflows Int16, so widen before taking it.
                    let magnitude = Float(abs(Int(sample))) / 32768.0
                    if magnitude > level { level = magnitude }
                    offset += frameBytes   // the left channel of each frame only
                }
            default:  // .f32 (and any future float form)
                let frameBytes = channelCount * 4
                var offset = 0
                while offset + 4 <= raw.count {
                    let sample = Float(bitPattern: UInt32(littleEndian:
                        raw.loadUnaligned(fromByteOffset: offset, as: UInt32.self)))
                    let magnitude = abs(sample)
                    if magnitude >= 1.0 {
                        currentRun += 1
                        if currentRun > clipRun { clipRun = currentRun }
                    } else {
                        currentRun = 0
                    }
                    if magnitude > level { level = magnitude }
                    offset += frameBytes
                }
            }
        }
        return (level: min(level, 1.0), clipRun: clipRun, openRun: currentRun)
    }

    /// Reads successive capture chunks as one continuous signal for the level
    /// meter, carrying the open full-scale run across each chunk boundary. The
    /// capture feeds every chunk, in order, through one instance; a clip that
    /// straddles a boundary is then one run, so sustained clipping is not lost by
    /// the three-sample rule just because it fell across a chunk edge (round 12).
    /// A fresh capture starts a fresh meter, so there is no reset.
    struct ClipRunMeter {
        /// The full-scale run still open at the end of the last chunk read (0
        /// when the last taken sample was not full scale).
        private(set) var carryRun = 0

        /// The peak of the next chunk, its clip run joined to any run carried
        /// from the previous chunk; advances the carry for the chunk after.
        mutating func peak(ofInterleaved data: Data,
                           sampleFormat: Beebium_AudioSampleFormat,
                           channels: Int) -> Peak {
            let scanned = LiveAudioInput.scan(ofInterleaved: data, sampleFormat: sampleFormat,
                                              channels: channels, carryRun: carryRun)
            carryRun = scanned.openRun
            return Peak(level: scanned.level, clipRun: scanned.clipRun)
        }
    }

    // MARK: - Re-chunking

    /// Re-chunks a stream of sample bytes into frame-aligned chunks of a fixed
    /// size, buffering the remainder. The capture tap delivers buffers of
    /// whatever size the system picks; this parcels them into the chunks the
    /// server expects without ever splitting a frame (the chunk size is a whole
    /// number of frames).
    struct Chunker {
        let chunkBytes: Int
        private var pending = Data()

        init(chunkBytes: Int) {
            self.chunkBytes = max(1, chunkBytes)
        }

        /// Add bytes, returning every complete chunk now available.
        mutating func take(_ incoming: Data) -> [Data] {
            pending.append(incoming)
            var chunks: [Data] = []
            while pending.count >= chunkBytes {
                chunks.append(Data(pending.prefix(chunkBytes)))
                pending.removeFirst(chunkBytes)
            }
            return chunks
        }

        /// The buffered remainder (a partial chunk), emptying the buffer. Sent
        /// as the final chunk when the stream is closed, so no captured sample
        /// is lost.
        mutating func drain() -> Data {
            let remainder = pending
            pending.removeAll(keepingCapacity: false)
            return remainder
        }
    }

    // MARK: - Device change

    /// The deck failure a device change earns: the sentence shown inline (with
    /// the section 3.4 affordance) and the copyable detail naming the device and
    /// what changed.
    struct DeviceChangeFailure: Equatable {
        let brief: String
        let detail: String
    }

    /// Decide what a capture-engine configuration change means for an open live
    /// stream (cassette round 12). `streamed` is the format `StreamAudio` was
    /// opened with; `current` is the input node's format after the change, or
    /// nil when the device has gone; `deviceListed` is whether the device id is
    /// still among the host's inputs.
    ///
    /// Returns nil while the device still delivers the streamed format (the
    /// stream carries on). A rate or channel-count change ends the stream with a
    /// sentence to choose the device again; a device that has gone (no format,
    /// no input channel, or no longer listed) ends it saying so. The channel
    /// count is compared as the stream takes it (at most two), so a device that
    /// stays stereo is unchanged. Pure, so the rule is tested without an engine.
    static func deviceChange(streamed: Beebium_AudioInputFormat,
                             current: AVAudioFormat?,
                             deviceListed: Bool) -> DeviceChangeFailure? {
        if let current, current.channelCount > 0, deviceListed {
            let newRate = Int(current.sampleRate.rounded())
            let newChannels = min(Int(current.channelCount), 2)
            let oldRate = Int(streamed.sampleRate)
            let oldChannels = Int(streamed.channels)
            if newRate == oldRate && newChannels == oldChannels { return nil }
            var detail = "\(streamed.deviceName): \(oldRate) Hz, \(oldChannels) channels, "
                + "now \(newRate) Hz"
            if newChannels != oldChannels { detail += ", \(newChannels) channels" }
            return DeviceChangeFailure(
                brief: "The input device changed while the live tape was playing; "
                    + "choose it again from Input Device.",
                detail: detail)
        }
        return DeviceChangeFailure(
            brief: "The input device was disconnected while the live tape was playing.",
            detail: "\(streamed.deviceName) is no longer available.")
    }

    // MARK: - Run-time gate

    /// Whether the running bundle carries a non-empty `NSMicrophoneUsageDescription`.
    /// The input-device picker is shown only when it does, so the app never asks
    /// the system for the microphone without the string present -- which would
    /// terminate it. The string is Rob's to supply; the client never adds it.
    static func usageStringPresent(in infoDictionary: [String: Any]?) -> Bool {
        guard let value = infoDictionary?["NSMicrophoneUsageDescription"] as? String else {
            return false
        }
        return !value.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
    }
}
