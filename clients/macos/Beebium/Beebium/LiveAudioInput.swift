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

    /// The peak and clip run of a chunk, read from the same little-endian
    /// interleaved bytes the deck streams (`samples(from:)`), in the stream's
    /// sample format. Only the left channel of a stereo chunk is read: clipping
    /// happens at the host's analogue-to-digital converter, which is the line
    /// the server reads, so a full-scale sample in the unread right channel must
    /// not light the mark. An S16 sample is normalised by 32768 (so the negative
    /// rail reads as full scale) and clips at either rail; an F32 sample clips at
    /// a magnitude of 1 or more. The clip run is the longest span of consecutive
    /// full-scale samples, so the meter can hold for sustained clipping and
    /// ignore an isolated click. Pure, so it is tested without hardware.
    static func peak(ofInterleaved data: Data,
                     sampleFormat: Beebium_AudioSampleFormat,
                     channels: Int) -> Peak {
        let channelCount = max(1, channels)
        var level: Float = 0
        var clipRun = 0
        var currentRun = 0
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
        return Peak(level: min(level, 1.0), clipRun: clipRun)
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
