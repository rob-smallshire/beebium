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

/// The mixer sidebar's buffer-health line reflects the ring buffer's underrun and
/// drop counters. This proves the publish path (ring buffer -> AudioClient ->
/// AudioMixerState.bufferHealth); the meter timer that drives it is started from
/// AudioMixerView.onAppear.
@MainActor
final class AudioMixerStateTests: XCTestCase {

    func testBufferHealthPublishesRingBufferUnderruns() {
        let client = AudioClient()
        let state = AudioMixerState()
        state.audioClient = client

        // Induce an underrun on the client's ring buffer: read more than is there.
        var dst = [UInt64](repeating: 0, count: 480)
        _ = dst.withUnsafeMutableBufferPointer {
            client.ringBuffer.read($0.baseAddress!, count: 480)
        }
        XCTAssertGreaterThan(client.bufferHealth().underrunEvents, 0, "sanity: the ring buffer underran")

        // Nothing is published until the poll runs.
        XCTAssertEqual(state.bufferHealth.underrunEvents, 0)

        // The poll (driven by the meter timer in the app) publishes it.
        state.updateMeterLevels()

        XCTAssertGreaterThan(state.bufferHealth.underrunEvents, 0,
                             "sidebar state must reflect ring-buffer underruns")
        XCTAssertEqual(state.bufferHealth, client.bufferHealth())
    }

    func testBufferHealthStaysZeroWhenThereAreNoGlitches() {
        let client = AudioClient()
        let state = AudioMixerState()
        state.audioClient = client
        state.updateMeterLevels()
        XCTAssertEqual(state.bufferHealth, AudioClient.BufferHealth())
    }
}
