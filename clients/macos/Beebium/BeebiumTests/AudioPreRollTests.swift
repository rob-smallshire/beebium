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

/// The #126 stutter reproduced offline: a producer and consumer at 48 kHz with an
/// injected stall. The old 85 ms fixed buffer underruns (silence); the adaptive
/// pre-roll rides the second stall of the same size after raising its target.
final class AudioPreRollTests: XCTestCase {

    private let sampleRate = 48000
    private let callbackFrames = 480          // a 10 ms render callback at 48 kHz

    private func millisToCallbacks(_ ms: Int) -> Int { ms / 10 }

    /// Write `frames` of (non-zero) audio into the buffer -- the producer.
    private func produce(_ ring: AudioRingBuffer, frames: Int) {
        let src = [UInt64](repeating: 1, count: frames)
        _ = src.withUnsafeBufferPointer { ring.write($0.baseAddress!, count: frames) }
    }

    /// One render callback through the pre-roll contract (exactly what
    /// AudioRenderer.render does): gate the read, and on a short read note the
    /// underrun. Returns the real frames delivered (the rest would be silence).
    @discardableResult
    private func consume(_ ring: AudioRingBuffer, _ preRoll: AudioPreRoll) -> Int {
        var dst = [UInt64](repeating: 0, count: callbackFrames)
        let toPull = preRoll.framesToPull(available: ring.available, requested: callbackFrames)
        let got = toPull > 0
            ? dst.withUnsafeMutableBufferPointer { ring.read($0.baseAddress!, count: toPull) }
            : 0
        if toPull > 0 && got < toPull { preRoll.noteUnderrun() }
        return got
    }

    /// One old-style callback: no pre-roll, always read the whole callback and let
    /// the ring buffer count any shortfall (the pre-#126 behaviour).
    private func consumeFixed(_ ring: AudioRingBuffer) {
        var dst = [UInt64](repeating: 0, count: callbackFrames)
        _ = dst.withUnsafeMutableBufferPointer { ring.read($0.baseAddress!, count: callbackFrames) }
    }

    /// Run `count` steady callbacks: produce one callback of audio, then consume
    /// one, so the buffer holds station around the target.
    private func runSteady(_ ring: AudioRingBuffer, _ preRoll: AudioPreRoll, callbacks count: Int) {
        for _ in 0..<count {
            produce(ring, frames: callbackFrames)
            consume(ring, preRoll)
        }
    }

    /// Frames for a duration at the test sample rate.
    private func frames(ms: Int) -> Int { ms * sampleRate / 1000 }

    // MARK: - The old fixed buffer stutters

    func testOldEightyFiveMillisecondBufferUnderrunsOnA150msStall() {
        // The pre-#126 buffer: 4096 samples (~85 ms), no pre-roll.
        let ring = AudioRingBuffer(capacity: 4096)
        // Fill it and reach steady state.
        produce(ring, frames: 4096)
        for _ in 0..<40 { produce(ring, frames: callbackFrames); consumeFixed(ring) }
        XCTAssertEqual(ring.totalUnderrunSamples, 0, "should be clean before the stall")

        // A 150 ms production stall: the consumer keeps pulling, nothing arrives.
        for _ in 0..<millisToCallbacks(150) { consumeFixed(ring) }

        // 150 ms > 85 ms of buffered audio, so it ran dry: silence was substituted.
        XCTAssertGreaterThan(ring.totalUnderrunSamples, 0,
                             "an 85 ms buffer must underrun on a 150 ms stall")
    }

    // MARK: - The adaptive pre-roll

    func testFirstStallUnderrunsOnceAndRaisesTheTargetSecondStallRidesThrough() {
        let ring = AudioRingBuffer(capacity: AudioPreRoll.capacityFrames(sampleRate: sampleRate))
        let preRoll = AudioPreRoll(sampleRate: sampleRate)

        // Prime and settle at the 120 ms floor.
        runSteady(ring, preRoll, callbacks: 60)
        XCTAssertFalse(preRoll.isPriming, "should be playing after warm-up")
        XCTAssertEqual(preRoll.targetFill, frames(ms: 120))
        XCTAssertEqual(ring.totalUnderrunEvents, 0)

        // First 150 ms stall: 150 ms > the 120 ms held, so it underruns -- but only
        // once, because the pre-roll then stops reading and re-primes.
        for _ in 0..<millisToCallbacks(150) { consume(ring, preRoll) }
        XCTAssertEqual(ring.totalUnderrunEvents, 1, "exactly one underrun event")
        XCTAssertEqual(preRoll.targetFill, frames(ms: 180), "target raised 120 -> 180 ms")
        XCTAssertTrue(preRoll.isPriming, "re-priming to the higher target")

        // Recover: refill to the new 180 ms target and resume playing.
        runSteady(ring, preRoll, callbacks: 80)
        XCTAssertFalse(preRoll.isPriming, "should be playing again after refill")
        let eventsBeforeSecond = ring.totalUnderrunEvents

        // Second identical 150 ms stall: 180 ms held now absorbs it, no underrun.
        for _ in 0..<millisToCallbacks(150) { consume(ring, preRoll) }
        XCTAssertEqual(ring.totalUnderrunEvents, eventsBeforeSecond,
                       "the raised target rides the second identical stall")
        XCTAssertEqual(preRoll.targetFill, frames(ms: 180), "target held (no further rise)")
    }

    func testLatencyStaysAtTheFloorWhenThereAreNoStalls() {
        let ring = AudioRingBuffer(capacity: AudioPreRoll.capacityFrames(sampleRate: sampleRate))
        let preRoll = AudioPreRoll(sampleRate: sampleRate)

        // A host that keeps up: many steady callbacks, no stall.
        runSteady(ring, preRoll, callbacks: 500)

        XCTAssertEqual(ring.totalUnderrunEvents, 0, "no underruns when the host keeps up")
        XCTAssertEqual(preRoll.targetFill, frames(ms: 120), "latency stays at the 120 ms floor")
        XCTAssertEqual(preRoll.targetFillMilliseconds, 120)
    }

    func testTargetRisesInSixtyMillisecondStepsAndCapsAt480() {
        let preRoll = AudioPreRoll(sampleRate: sampleRate)
        XCTAssertEqual(preRoll.targetFill, frames(ms: 120))
        let expected = [180, 240, 300, 360, 420, 480, 480, 480]
        for ms in expected {
            preRoll.noteUnderrun()
            XCTAssertEqual(preRoll.targetFill, frames(ms: ms))
        }
        preRoll.reset()
        XCTAssertEqual(preRoll.targetFill, frames(ms: 120), "reset returns to the floor")
        XCTAssertTrue(preRoll.isPriming)
    }

    // MARK: - Ring-buffer underrun counting

    func testReadShortCountsUnderrunSamplesAndEvents() {
        let ring = AudioRingBuffer(capacity: 4096)
        produce(ring, frames: 100)
        var dst = [UInt64](repeating: 0, count: 480)
        let got = dst.withUnsafeMutableBufferPointer { ring.read($0.baseAddress!, count: 480) }
        XCTAssertEqual(got, 100)
        XCTAssertEqual(ring.totalUnderrunSamples, 380)   // 480 asked, 100 delivered
        XCTAssertEqual(ring.totalUnderrunEvents, 1)
    }

    func testReadWithinAvailableCountsNoUnderrun() {
        let ring = AudioRingBuffer(capacity: 4096)
        produce(ring, frames: 480)
        var dst = [UInt64](repeating: 0, count: 480)
        _ = dst.withUnsafeMutableBufferPointer { ring.read($0.baseAddress!, count: 480) }
        XCTAssertEqual(ring.totalUnderrunSamples, 0)
        XCTAssertEqual(ring.totalUnderrunEvents, 0)
    }
}
