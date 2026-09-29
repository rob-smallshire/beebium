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

import XCTest
@testable import Beebium

final class AudioRendererTests: XCTestCase {

    // Pack one frame with all four SN76489 channels at the same signed value:
    // source 0 = (tone0, tone1), source 1 = (tone2, noise), low half first.
    private func frame(all value: Int16) -> UInt64 {
        let u = UInt32(UInt16(bitPattern: value))
        let word = u | (u << 16)
        return UInt64(word) | (UInt64(word) << 32)
    }

    // A volume-0 onset on all four channels must never drive the output beyond
    // the valid float range, even during the 10 Hz high-pass settling transient
    // where the unipolar DC step pushes the per-channel signal toward full scale.
    func testVolume0OnsetOnAllChannelsStaysInRange() {
        let chunk = 4096
        let chunks = 3   // > the pre-roll's 120 ms (5760 frames) target, so it plays
        let ring = AudioRingBuffer(capacity: AudioPreRoll.capacityFrames(sampleRate: 48000))
        let renderer = AudioRenderer(ringBuffer: ring, sampleRate: 48000, maxFrameCount: chunk)

        // Volume 0 (max): high level is Sn76489 full scale (16384). A square that
        // starts from silence is an onset on every channel at once. Buffer several
        // chunks up front so the pre-roll primes and then plays from the onset.
        let count = chunk * chunks
        var frames = [UInt64](repeating: 0, count: count)
        for i in 0..<count {
            frames[i] = frame(all: (i % 2 == 0) ? 16384 : 0)
        }
        let written = frames.withUnsafeBufferPointer {
            ring.write($0.baseAddress!, count: count)
        }
        XCTAssertEqual(written, count)

        // Render the buffered audio one callback at a time and check every sample.
        var peak: Float = 0
        for _ in 0..<chunks {
            var left = [Float](repeating: 0, count: chunk)
            var right = [Float](repeating: 0, count: chunk)
            let rendered = left.withUnsafeMutableBufferPointer { lb in
                right.withUnsafeMutableBufferPointer { rb in
                    renderer.render(frameCount: chunk,
                                    leftBuffer: lb.baseAddress!,
                                    rightBuffer: rb.baseAddress!)
                }
            }
            XCTAssertEqual(rendered, chunk)
            for v in left + right {
                XCTAssertTrue(v.isFinite, "non-finite output sample")
                XCTAssertGreaterThanOrEqual(v, -1.0, "output below -1.0 (hard clip)")
                XCTAssertLessThanOrEqual(v, 1.0, "output above +1.0 (hard clip)")
                peak = max(peak, abs(v))
            }
        }
        // The pipeline should produce real signal, not silence.
        XCTAssertGreaterThan(peak, 0.1)
    }

    // MARK: - Mix gain

    // Render a full-scale (volume 0) square at `frequencyHz` on the given
    // channels, all in phase and centre-panned at master volume 1.0, and return
    // the output after the 10 Hz high-pass has settled from the onset (which
    // briefly doubles the AC swing as it removes the unipolar DC).
    private func renderSquare(onChannels channels: Set<Int>, frequencyHz: Int) -> (left: [Float], right: [Float]) {
        let sampleRate = 48000
        let chunk = 4096
        let settleChunks = 3       // 0.26 s, many time constants of the 10 Hz high-pass
        let measureChunks = 2
        let halfPeriod = sampleRate / (2 * frequencyHz)
        let ring = AudioRingBuffer(capacity: chunk * 2)
        let renderer = AudioRenderer(ringBuffer: ring, sampleRate: Float(sampleRate), maxFrameCount: chunk)

        var measuredLeft: [Float] = []
        var measuredRight: [Float] = []
        var n = 0
        for chunkIndex in 0..<(settleChunks + measureChunks) {
            var frames = [UInt64](repeating: 0, count: chunk)
            for i in 0..<chunk {
                let high: Int16 = ((n / halfPeriod) % 2 == 0) ? 16384 : 0
                let tone0 = channels.contains(0) ? high : 0
                let tone1 = channels.contains(1) ? high : 0
                let tone2 = channels.contains(2) ? high : 0
                let noise = channels.contains(3) ? high : 0
                let source0 = UInt32(UInt16(bitPattern: tone0)) | (UInt32(UInt16(bitPattern: tone1)) << 16)
                let source1 = UInt32(UInt16(bitPattern: tone2)) | (UInt32(UInt16(bitPattern: noise)) << 16)
                frames[i] = UInt64(source0) | (UInt64(source1) << 32)
                n += 1
            }
            let written = frames.withUnsafeBufferPointer { ring.write($0.baseAddress!, count: chunk) }
            XCTAssertEqual(written, chunk)

            var left = [Float](repeating: 0, count: chunk)
            var right = [Float](repeating: 0, count: chunk)
            let rendered = left.withUnsafeMutableBufferPointer { lb in
                right.withUnsafeMutableBufferPointer { rb in
                    renderer.render(frameCount: chunk, leftBuffer: lb.baseAddress!, rightBuffer: rb.baseAddress!)
                }
            }
            XCTAssertEqual(rendered, chunk)
            if chunkIndex >= settleChunks {
                measuredLeft += left
                measuredRight += right
            }
        }
        return (measuredLeft, measuredRight)
    }

    private func peak(_ samples: [Float]) -> Float {
        samples.reduce(0) { max($0, abs($1)) }
    }

    func testMixGainIsTheChipWeightingOverTheHighpassWorstCasePeak() {
        XCTAssertEqual(AudioRenderer.mixGain, 0.2125)
    }

    // One full-volume channel at 1 kHz: its measured peak per side, recorded
    // (not modelled) so a change anywhere in the chain shows up here.
    // tools/audio-analysis/client_chain.py reproduces this value.
    func testOneFullScaleChannelAt1kHzPeaksAtItsMeasuredLevel() {
        let one = renderSquare(onChannels: [0], frequencyHz: 1000)
        let peakLeft = peak(one.left)
        print("mix gain: one channel at 1 kHz peaks at \(peakLeft) per side")
        XCTAssertEqual(peakLeft, 0.1535, accuracy: 0.0005)
        XCTAssertEqual(peakLeft, peak(one.right), accuracy: 1e-6)
    }

    // The worst case for chip output: all four channels at full volume, in
    // phase, centre-panned, master 1.0. At each tone frequency the mix stays
    // at or under 0.72 per side, below the limiter's 0.8 knee, so the limiter
    // is the identity: the four-channel output is the linear sum, four times
    // the one-channel output, sample for sample. 125 Hz is the high-pass's
    // worst case; 1 kHz and 6 kHz cover the rest of the range.
    func testFourFullScaleChannelsInPhaseMixLinearlyUnderTheKnee() {
        for frequencyHz in [125, 1000, 6000] {
            let one = renderSquare(onChannels: [0], frequencyHz: frequencyHz)
            let four = renderSquare(onChannels: [0, 1, 2, 3], frequencyHz: frequencyHz)

            let peakLeft = peak(four.left)
            let peakRight = peak(four.right)
            print("mix gain: four channels at \(frequencyHz) Hz peak at \(peakLeft) / \(peakRight) per side")
            XCTAssertLessThanOrEqual(peakLeft, 0.72, "left peak at \(frequencyHz) Hz")
            XCTAssertLessThanOrEqual(peakRight, 0.72, "right peak at \(frequencyHz) Hz")

            var worstDeviation: Float = 0
            for i in 0..<four.left.count {
                worstDeviation = max(worstDeviation, abs(four.left[i] - 4 * one.left[i]))
                worstDeviation = max(worstDeviation, abs(four.right[i] - 4 * one.right[i]))
            }
            XCTAssertLessThanOrEqual(worstDeviation, 1e-4, "not the linear sum at \(frequencyHz) Hz: the limiter engaged")
        }
    }

    // MARK: - Mixer channel listing

    // The SN76489 is described as two sources sharing one group. The mixer maps
    // each channel to a mixer channel index by concatenating the group's sources
    // in source-index order (0..3 = tone0, tone1, tone2, noise), then DISPLAYS
    // the rows ordered by channel name numerically (0, 1, 2, 3 -- noise first).
    // The (index, name) pairing is unchanged: name "0" stays mixer channel 3.
    func testChannelsInGroupDisplaysNumericNameOrderWithStableIndexPairing() {
        // Deliberately out of source-index order to prove the function sorts.
        let sources = [
            AudioSourceInfo(id: 1, name: "SN76489", channelNames: ["3", "0"], groupId: 1),
            AudioSourceInfo(id: 3, name: "Reserved", channelNames: [], groupId: 0),
            AudioSourceInfo(id: 0, name: "SN76489", channelNames: ["1", "2"], groupId: 1),
        ]

        let channels = AudioSourceInfo.channelsInGroup(sources, groupId: 1)

        // Displayed noise-first, but "0" still controls mixer channel 3.
        XCTAssertEqual(channels.map { $0.0 }, [3, 0, 1, 2])
        XCTAssertEqual(channels.map { $0.1 }, ["0", "1", "2", "3"])
    }

    // Numeric ordering compares as integers, not strings, so "10" follows "2".
    func testChannelsInGroupOrdersNamesAsIntegersNotStrings() {
        let sources = [
            AudioSourceInfo(id: 0, name: "X", channelNames: ["2", "10"], groupId: 1),
            AudioSourceInfo(id: 1, name: "X", channelNames: ["1"], groupId: 1),
        ]

        let channels = AudioSourceInfo.channelsInGroup(sources, groupId: 1)

        XCTAssertEqual(channels.map { $0.1 }, ["1", "2", "10"])
        // "1" is source 1's only channel -> mixer index 2; "2","10" are source 0.
        XCTAssertEqual(channels.map { $0.0 }, [2, 0, 1])
    }

    // If any name in the group is non-numeric, keep source (mixer-index) order.
    func testChannelsInGroupKeepsSourceOrderWhenNamesAreNotAllNumeric() {
        let sources = [
            AudioSourceInfo(id: 0, name: "Speech", channelNames: ["Left", "Right"], groupId: 2),
            AudioSourceInfo(id: 1, name: "Speech", channelNames: ["Mono"], groupId: 2),
        ]

        let channels = AudioSourceInfo.channelsInGroup(sources, groupId: 2)

        XCTAssertEqual(channels.map { $0.0 }, [0, 1, 2])
        XCTAssertEqual(channels.map { $0.1 }, ["Left", "Right", "Mono"])
    }

    func testChannelsInUnknownGroupIsEmpty() {
        let sources = [
            AudioSourceInfo(id: 0, name: "SN76489", channelNames: ["1", "2"], groupId: 1),
            AudioSourceInfo(id: 1, name: "SN76489", channelNames: ["3", "0"], groupId: 1),
        ]

        XCTAssertTrue(AudioSourceInfo.channelsInGroup(sources, groupId: 99).isEmpty)
    }
}
