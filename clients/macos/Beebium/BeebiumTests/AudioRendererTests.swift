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
    // the valid float range, even during the 20 Hz high-pass settling transient
    // where the unipolar DC step pushes the per-channel signal toward full scale.
    func testVolume0OnsetOnAllChannelsStaysInRange() {
        let ring = AudioRingBuffer(capacity: 8192)
        let renderer = AudioRenderer(ringBuffer: ring, sampleRate: 48000, maxFrameCount: 4096)

        // Volume 0 (max): high level is Sn76489 full scale (16384). A square that
        // starts from silence is an onset on every channel at once.
        let count = 4096
        var frames = [UInt64](repeating: 0, count: count)
        for i in 0..<count {
            frames[i] = frame(all: (i % 2 == 0) ? 16384 : 0)
        }
        let written = frames.withUnsafeBufferPointer {
            ring.write($0.baseAddress!, count: count)
        }
        XCTAssertEqual(written, count)

        var left = [Float](repeating: 0, count: count)
        var right = [Float](repeating: 0, count: count)
        let rendered = left.withUnsafeMutableBufferPointer { lb in
            right.withUnsafeMutableBufferPointer { rb in
                renderer.render(frameCount: count,
                                leftBuffer: lb.baseAddress!,
                                rightBuffer: rb.baseAddress!)
            }
        }
        XCTAssertEqual(rendered, count)

        var peak: Float = 0
        for v in left + right {
            XCTAssertTrue(v.isFinite, "non-finite output sample")
            XCTAssertGreaterThanOrEqual(v, -1.0, "output below -1.0 (hard clip)")
            XCTAssertLessThanOrEqual(v, 1.0, "output above +1.0 (hard clip)")
            peak = max(peak, abs(v))
        }
        // The pipeline should produce real signal, not silence.
        XCTAssertGreaterThan(peak, 0.1)
    }

    // MARK: - Mixer channel listing

    // The SN76489 is described as two sources sharing one group; the mixer must
    // list the channels of every source in the group, concatenated in
    // source-index order, so the running position is the mixer channel index
    // (0..3 = tone0, tone1, tone2, noise). Reserved sources sit in another group.
    func testChannelsInGroupConcatenatesSourcesInIndexOrder() {
        // Deliberately out of source-index order to prove the function sorts.
        let sources = [
            AudioSourceInfo(id: 1, name: "SN76489", channelNames: ["3", "0"], groupId: 1),
            AudioSourceInfo(id: 3, name: "Reserved", channelNames: [], groupId: 0),
            AudioSourceInfo(id: 0, name: "SN76489", channelNames: ["1", "2"], groupId: 1),
        ]

        let channels = AudioSourceInfo.channelsInGroup(sources, groupId: 1)

        XCTAssertEqual(channels.map { $0.0 }, [0, 1, 2, 3])
        XCTAssertEqual(channels.map { $0.1 }, ["1", "2", "3", "0"])
    }

    func testChannelsInUnknownGroupIsEmpty() {
        let sources = [
            AudioSourceInfo(id: 0, name: "SN76489", channelNames: ["1", "2"], groupId: 1),
            AudioSourceInfo(id: 1, name: "SN76489", channelNames: ["3", "0"], groupId: 1),
        ]

        XCTAssertTrue(AudioSourceInfo.channelsInGroup(sources, groupId: 99).isEmpty)
    }
}
