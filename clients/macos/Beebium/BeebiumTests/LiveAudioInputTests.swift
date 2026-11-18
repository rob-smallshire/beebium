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
import XCTest
@testable import Beebium

/// Tests for the pure pieces of live audio input: the device-to-stream format
/// mapping, the interleaving and sample-format encoding, the re-chunking into
/// server-sized chunks, and the microphone-usage-string gate.
final class LiveAudioInputTests: XCTestCase {

    private func format(_ common: AVAudioCommonFormat, _ rate: Double, _ channels: AVAudioChannelCount)
        -> AVAudioFormat {
        AVAudioFormat(commonFormat: common, sampleRate: rate, channels: channels, interleaved: false)!
    }

    // MARK: - inputFormat

    func testStereoFloatDeviceMapsToTheStreamFormat() {
        let result = LiveAudioInput.inputFormat(
            from: format(.pcmFormatFloat32, 48000, 2), deviceName: "Line In")
        XCTAssertEqual(result?.sampleRate, 48000)
        XCTAssertEqual(result?.sampleFormat, .f32)
        XCTAssertEqual(result?.channels, 2)
        XCTAssertEqual(result?.channel, "left", "a stereo stream takes the left channel by default")
        XCTAssertEqual(result?.deviceName, "Line In")
    }

    func testMonoDeviceCarriesNoChannelSelection() {
        let result = LiveAudioInput.inputFormat(
            from: format(.pcmFormatFloat32, 44100, 1), deviceName: "Built-in Microphone")
        XCTAssertEqual(result?.channels, 1)
        XCTAssertEqual(result?.channel, "", "a mono stream names no channel")
        XCTAssertEqual(result?.sampleRate, 44100)
    }

    func testInt16DeviceMapsToS16() {
        let result = LiveAudioInput.inputFormat(
            from: format(.pcmFormatInt16, 22050, 2), deviceName: "USB Audio")
        XCTAssertEqual(result?.sampleFormat, .s16)
        XCTAssertEqual(result?.sampleRate, 22050)
    }

    func testAMultiChannelDeviceIsTakenAsStereo() {
        // A format with more than two channels needs an explicit layout.
        let layout = AVAudioChannelLayout(
            layoutTag: kAudioChannelLayoutTag_DiscreteInOrder | 8)!
        let eightChannel = AVAudioFormat(commonFormat: .pcmFormatFloat32, sampleRate: 48000,
                                         interleaved: false, channelLayout: layout)
        let result = LiveAudioInput.inputFormat(from: eightChannel, deviceName: "Interface")
        XCTAssertEqual(result?.channels, 2, "more than two inputs contribute their first two")
        XCTAssertEqual(result?.channel, "left")
    }

    func testRateOutOfRangeIsRejected() {
        XCTAssertNil(LiveAudioInput.inputFormat(from: format(.pcmFormatFloat32, 4000, 1),
                                                deviceName: "Slow"))
        XCTAssertNil(LiveAudioInput.inputFormat(from: format(.pcmFormatFloat32, 200000, 1),
                                                deviceName: "Fast"))
    }

    func testUnsupportedSampleFormatIsRejected() {
        XCTAssertNil(LiveAudioInput.inputFormat(from: format(.pcmFormatInt32, 48000, 1),
                                                deviceName: "Int32"))
    }

    // MARK: - frameBytes / framesPerChunk

    func testFrameBytes() {
        var stereoF32 = Beebium_AudioInputFormat()
        stereoF32.sampleFormat = .f32
        stereoF32.channels = 2
        XCTAssertEqual(LiveAudioInput.frameBytes(for: stereoF32), 8)

        var monoS16 = Beebium_AudioInputFormat()
        monoS16.sampleFormat = .s16
        monoS16.channels = 1
        XCTAssertEqual(LiveAudioInput.frameBytes(for: monoS16), 2)
    }

    func testFramesPerChunkIsAboutFifteenMilliseconds() {
        XCTAssertEqual(LiveAudioInput.framesPerChunk(sampleRate: 48000), 720)
        XCTAssertEqual(LiveAudioInput.framesPerChunk(sampleRate: 44100), 662)   // 661.5 rounds up
        XCTAssertEqual(LiveAudioInput.framesPerChunk(sampleRate: 8000), 120)
    }

    // MARK: - interleaving and encoding

    func testFloatChannelsAreInterleavedAsLittleEndianFloats() {
        let data = LiveAudioInput.interleavedLittleEndian(
            channels: [[0.5, -0.5], [0.25, -0.25]], sampleFormat: .f32)
        // Two frames, two channels, four bytes each: L0 R0 L1 R1.
        XCTAssertEqual(data.count, 2 * 2 * 4)
        let values = data.withUnsafeBytes { raw -> [Float] in
            stride(from: 0, to: raw.count, by: 4).map { offset in
                Float(bitPattern: UInt32(littleEndian: raw.loadUnaligned(fromByteOffset: offset,
                                                                         as: UInt32.self)))
            }
        }
        XCTAssertEqual(values, [0.5, 0.25, -0.5, -0.25])
    }

    func testFloatIsEncodedAndClampedAsSignedSixteenBit() {
        let data = LiveAudioInput.interleavedLittleEndian(
            channels: [[1.0, -1.0, 0.0, 2.0]], sampleFormat: .s16)
        XCTAssertEqual(data.count, 4 * 2)
        let values = data.withUnsafeBytes { raw -> [Int16] in
            stride(from: 0, to: raw.count, by: 2).map { offset in
                Int16(littleEndian: raw.loadUnaligned(fromByteOffset: offset, as: Int16.self))
            }
        }
        // Full scale maps to +/-32767; silence to 0; an over-unity sample clamps.
        XCTAssertEqual(values, [32767, -32767, 0, 32767])
    }

    func testInterleavingTakesTheShorterChannelLength() {
        let data = LiveAudioInput.interleavedLittleEndian(
            channels: [[0.0, 0.0, 0.0], [0.0]], sampleFormat: .f32)
        XCTAssertEqual(data.count, 1 * 2 * 4, "frames are taken only where both channels have samples")
    }

    func testBufferSamplesTakeTheRequestedChannels() {
        let fmt = format(.pcmFormatFloat32, 48000, 2)
        let buffer = AVAudioPCMBuffer(pcmFormat: fmt, frameCapacity: 2)!
        buffer.frameLength = 2
        buffer.floatChannelData![0][0] = 0.5
        buffer.floatChannelData![0][1] = 0.25
        buffer.floatChannelData![1][0] = -0.5
        buffer.floatChannelData![1][1] = -0.25
        let data = LiveAudioInput.samples(from: buffer, channels: 2, sampleFormat: .f32)
        XCTAssertEqual(data.count, 2 * 2 * 4)
    }

    // MARK: - Chunker

    func testChunkerEmitsFixedSizeChunksAndBuffersTheRemainder() {
        var chunker = LiveAudioInput.Chunker(chunkBytes: 8)
        XCTAssertEqual(chunker.take(Data(repeating: 1, count: 20)).map(\.count), [8, 8],
                       "two full chunks from 20 bytes, 4 left over")
        XCTAssertEqual(chunker.take(Data(repeating: 2, count: 3)).map(\.count), [],
                       "4 + 3 is under a chunk, so none emitted yet")
        XCTAssertEqual(chunker.take(Data(repeating: 3, count: 5)).map(\.count), [8],
                       "7 + 5 completes one chunk, 4 left over")
        XCTAssertEqual(chunker.drain().count, 4)
        XCTAssertTrue(chunker.drain().isEmpty, "draining empties the buffer")
    }

    func testChunkerLosesNoBytes() {
        var chunker = LiveAudioInput.Chunker(chunkBytes: 10)
        var reassembled = Data()
        for n in [7, 5, 11, 1, 20] {
            for chunk in chunker.take(Data(repeating: UInt8(n), count: n)) { reassembled.append(chunk) }
        }
        reassembled.append(chunker.drain())
        XCTAssertEqual(reassembled.count, 7 + 5 + 11 + 1 + 20)
    }

    // MARK: - Peak / clip metering

    /// Signed 16-bit little-endian interleaved bytes from raw samples, so a
    /// chunk can carry an exact rail (which the float encoder cannot reach).
    private func s16(_ samples: [Int16]) -> Data {
        var data = Data(capacity: samples.count * 2)
        for sample in samples {
            withUnsafeBytes(of: UInt16(bitPattern: sample).littleEndian) { data.append(contentsOf: $0) }
        }
        return data
    }

    func testPeakOfAMonoFloatChunk() {
        let data = LiveAudioInput.interleavedLittleEndian(
            channels: [[0.25, -0.5, 0.1]], sampleFormat: .f32)
        let peak = LiveAudioInput.peak(ofInterleaved: data, sampleFormat: .f32, channels: 1)
        XCTAssertEqual(peak.level, 0.5, accuracy: 1e-6)
        XCTAssertFalse(peak.clipped)
    }

    func testPeakOfAMonoS16Chunk() {
        // -16384 / 32768 = 0.5.
        let peak = LiveAudioInput.peak(ofInterleaved: s16([8192, -16384, 4096]),
                                       sampleFormat: .s16, channels: 1)
        XCTAssertEqual(peak.level, 0.5, accuracy: 1e-4)
        XCTAssertFalse(peak.clipped)
    }

    func testFloatClipMarkLightsAtFullScale() {
        let data = LiveAudioInput.interleavedLittleEndian(
            channels: [[0.2, 1.0, 0.3]], sampleFormat: .f32)
        let peak = LiveAudioInput.peak(ofInterleaved: data, sampleFormat: .f32, channels: 1)
        XCTAssertTrue(peak.clipped)
        XCTAssertEqual(peak.level, 1.0, accuracy: 1e-6)
    }

    func testS16ClipMarkLightsAtEitherRail() {
        XCTAssertTrue(LiveAudioInput.peak(ofInterleaved: s16([100, 32767]),
                                          sampleFormat: .s16, channels: 1).clipped,
                      "the positive rail clips")
        let negative = LiveAudioInput.peak(ofInterleaved: s16([-32768, 50]),
                                           sampleFormat: .s16, channels: 1)
        XCTAssertTrue(negative.clipped, "the negative rail clips")
        XCTAssertEqual(negative.level, 1.0, accuracy: 1e-6, "the negative rail is full scale")
    }

    func testStereoFloatTakesOnlyTheLeftChannel() {
        // Frames (L, R): (0.3, 1.0), (0.2, 1.0). The right channel is at full
        // scale and must neither set the level nor light the mark.
        let data = LiveAudioInput.interleavedLittleEndian(
            channels: [[0.3, 0.2], [1.0, 1.0]], sampleFormat: .f32)
        let peak = LiveAudioInput.peak(ofInterleaved: data, sampleFormat: .f32, channels: 2)
        XCTAssertEqual(peak.level, 0.3, accuracy: 1e-6, "only the left channel feeds the meter")
        XCTAssertFalse(peak.clipped, "a full-scale right channel must not light the clip mark")
    }

    func testStereoS16TakesOnlyTheLeftChannel() {
        // Frames (L, R): (4096, 32767), (8192, 32767).
        let peak = LiveAudioInput.peak(ofInterleaved: s16([4096, 32767, 8192, 32767]),
                                       sampleFormat: .s16, channels: 2)
        XCTAssertEqual(peak.level, 8192.0 / 32768.0, accuracy: 1e-4)
        XCTAssertFalse(peak.clipped, "the right channel's full scale is not taken")
    }

    func testPeakOfAnEmptyChunkIsSilent() {
        XCTAssertEqual(LiveAudioInput.peak(ofInterleaved: Data(), sampleFormat: .f32, channels: 1),
                       .silent)
    }

    // MARK: - Clip run (round 12)

    func testClipRunIsTheLongestConsecutiveFullScaleSpanS16() {
        // Runs of 2 then 3 full-scale samples (either rail counts); the longest
        // is 3. A single isolated rail sample does not extend a run.
        let peak = LiveAudioInput.peak(
            ofInterleaved: s16([32767, 32767, 100, -32768, -32768, -32768, 5, 32767]),
            sampleFormat: .s16, channels: 1)
        XCTAssertEqual(peak.clipRun, 3)
        XCTAssertEqual(peak.level, 1.0, accuracy: 1e-6)
        XCTAssertTrue(peak.clipped, "a run of full-scale samples reads as clipped")
    }

    func testClipRunIsTheLongestConsecutiveFullScaleSpanFloat() {
        let data = LiveAudioInput.interleavedLittleEndian(
            channels: [[0.2, 1.0, 1.0, 1.0, 0.3, 1.0]], sampleFormat: .f32)
        let peak = LiveAudioInput.peak(ofInterleaved: data, sampleFormat: .f32, channels: 1)
        XCTAssertEqual(peak.clipRun, 3)
    }

    func testAnIsolatedFullScaleSampleIsARunOfOne() {
        let data = LiveAudioInput.interleavedLittleEndian(
            channels: [[0.2, 1.0, 0.3]], sampleFormat: .f32)
        let peak = LiveAudioInput.peak(ofInterleaved: data, sampleFormat: .f32, channels: 1)
        XCTAssertEqual(peak.clipRun, 1, "a single-sample click is a run of one, not sustained")
        XCTAssertTrue(peak.clipped)
    }

    func testClipRunCountsOnlyTheTakenLeftChannel() {
        // Stereo frames (L, R): the right channel is at full scale three in a
        // row, the left never is; the clip run is read from the left only.
        let data = LiveAudioInput.interleavedLittleEndian(
            channels: [[0.3, 0.2, 0.4], [1.0, 1.0, 1.0]], sampleFormat: .f32)
        let peak = LiveAudioInput.peak(ofInterleaved: data, sampleFormat: .f32, channels: 2)
        XCTAssertEqual(peak.clipRun, 0, "a full-scale right channel does not count")
    }

    func testClipRunOfAnEmptyChunkIsZero() {
        XCTAssertEqual(
            LiveAudioInput.peak(ofInterleaved: Data(), sampleFormat: .s16, channels: 1).clipRun, 0)
    }

    // MARK: - Usage string gate

    func testUsageStringGate() {
        XCTAssertTrue(LiveAudioInput.usageStringPresent(
            in: ["NSMicrophoneUsageDescription": "Beebium streams a tape deck to the cassette input."]))
        XCTAssertFalse(LiveAudioInput.usageStringPresent(in: [:]))
        XCTAssertFalse(LiveAudioInput.usageStringPresent(in: nil))
        XCTAssertFalse(LiveAudioInput.usageStringPresent(in: ["NSMicrophoneUsageDescription": "  "]),
                       "a blank string is as good as absent")
        XCTAssertFalse(LiveAudioInput.usageStringPresent(in: ["NSMicrophoneUsageDescription": 42]),
                       "a non-string value does not count")
    }
}
