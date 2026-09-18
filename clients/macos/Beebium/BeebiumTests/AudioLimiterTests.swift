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

// The output limiter must be a smooth soft-knee curve. A discontinuity (as in a
// naive hard/exponential clip) turns every crossing of the limit by a
// multi-channel mix into a full-scale step -- an audible pop or crackle.
final class AudioLimiterTests: XCTestCase {

    private let knee: Float = 0.8

    func testIdentityInsideKnee() {
        var x: Float = -knee
        while x <= knee {
            XCTAssertEqual(AudioRenderer.softLimit(x), x, accuracy: 1e-6)
            x += 0.01
        }
    }

    func testOddSymmetry() {
        var x: Float = 0.0
        while x <= 8.0 {
            XCTAssertEqual(AudioRenderer.softLimit(-x), -AudioRenderer.softLimit(x), accuracy: 1e-5)
            x += 0.013
        }
    }

    func testBoundedBelowOne() {
        var x: Float = -8.0
        while x <= 8.0 {
            XCTAssertLessThan(abs(AudioRenderer.softLimit(x)), 1.0)
            x += 0.001
        }
    }

    func testContinuousAndMonotonic() {
        let dx: Float = 0.001
        var prevX: Float = -8.0
        var prevY = AudioRenderer.softLimit(prevX)
        var x = prevX + dx
        while x <= 8.0 {
            let y = AudioRenderer.softLimit(x)
            // Continuous: adjacent outputs differ by no more than the input step
            // (the slope never exceeds 1).
            XCTAssertLessThanOrEqual(abs(y - prevY), dx * 1.01,
                                     "discontinuity near x=\(x): \(prevY) -> \(y)")
            // Monotonic non-decreasing.
            XCTAssertGreaterThanOrEqual(y, prevY - 1e-6, "not monotonic near x=\(x)")
            prevX = x
            prevY = y
            x += dx
        }
    }

    func testUnitSlopeAtKneeBothSides() {
        let e: Float = 1e-4
        let below = (AudioRenderer.softLimit(knee) - AudioRenderer.softLimit(knee - e)) / e
        let above = (AudioRenderer.softLimit(knee + e) - AudioRenderer.softLimit(knee)) / e
        XCTAssertEqual(below, 1.0, accuracy: 1e-2)
        XCTAssertEqual(above, 1.0, accuracy: 1e-2)  // C1: slope matches across the knee
    }

    // Negative controls: the specific broken values a discontinuous clip
    // produces must not occur.
    func testNoBrokenValues() {
        // Just past the +knee the output must stay near full scale, not collapse.
        XCTAssertGreaterThan(AudioRenderer.softLimit(1.0001), 0.5)
        // A large negative input must stay negative and within range.
        XCTAssertLessThan(AudioRenderer.softLimit(-2.0), 0.0)
        XCTAssertGreaterThan(AudioRenderer.softLimit(-2.0), -1.0)
    }
}
