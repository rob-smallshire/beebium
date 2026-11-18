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

import XCTest
@testable import Beebium

/// Tests for the live input level meter's decibel state (cassette round 12):
/// the dB conversion, the bar's fall, the peak hold's dwell and fall, and the
/// sustained-clip mark's hold. Pure and time-driven, so no hardware or clock.
final class LevelMeterStateTests: XCTestCase {

    /// A linear level (0..1) for a decibel value, the inverse of the meter's dB.
    private func level(forDB db: Float) -> Float { pow(10, db / 20) }

    /// The bar fraction a decibel value maps to, for comparing outputs.
    private func fraction(ofDB db: Float) -> Float {
        (db - LevelMeterState.floorDB) / -LevelMeterState.floorDB
    }

    /// The decibel value a bar fraction stands for, the inverse of `fraction`.
    private func decibels(ofFraction fraction: Float) -> Float {
        LevelMeterState.floorDB + fraction * -LevelMeterState.floorDB
    }

    // MARK: - dB conversion

    func testFullScaleIsZeroDecibelsAndFillsTheBar() {
        XCTAssertEqual(LevelMeterState.decibels(ofLevel: 1.0), 0, accuracy: 1e-5)
        XCTAssertEqual(LevelMeterState.fraction(ofDB: 0), 1, accuracy: 1e-6)
    }

    func testTheTargetTickSitsAtMinusTwelveDecibels() {
        XCTAssertEqual(LevelMeterState.decibels(ofLevel: level(forDB: -12)), -12, accuracy: 1e-4)
        // -12 of -60..0 is 48/60 = 0.8 of the bar.
        XCTAssertEqual(LevelMeterState.fraction(ofDB: -12), 0.8, accuracy: 1e-6)
        XCTAssertEqual(LevelMeterState.targetFraction, 0.8, accuracy: 1e-6)
    }

    func testSilenceAndTheFloorDrawNothing() {
        XCTAssertEqual(LevelMeterState.decibels(ofLevel: 0), -.infinity)
        XCTAssertEqual(LevelMeterState.fraction(ofDB: -.infinity), 0, accuracy: 1e-6)
        XCTAssertEqual(LevelMeterState.fraction(ofDB: LevelMeterState.floorDB), 0, accuracy: 1e-6)
        XCTAssertEqual(LevelMeterState.fraction(ofDB: -80), 0, accuracy: 1e-6)
    }

    // MARK: - Bar

    func testTheBarRisesAtOnceAndFallsAtTheBarRate() {
        var meter = LevelMeterState()
        meter.update(level: level(forDB: -6), clipRun: 0, at: 0)
        XCTAssertEqual(meter.barFraction, fraction(ofDB: -6), accuracy: 1e-4,
                       "the bar attacks instantly to the signal")
        // 0.1 s of near-silence later: -6 dB falls 20 dB/s * 0.1 s = 2 dB to -8.
        meter.update(level: level(forDB: -60), clipRun: 0, at: 0.1)
        XCTAssertEqual(decibels(ofFraction: meter.barFraction), -8, accuracy: 0.1,
                       "the bar falls at 20 dB/s")
    }

    // MARK: - Peak hold

    func testThePeakHoldSitsForTwoSecondsThenFallsAndIsOvertakenByALouderBar() {
        var meter = LevelMeterState()
        meter.update(level: level(forDB: -6), clipRun: 0, at: 0)      // peak, hold armed at t=0
        // Still within the two-second dwell: the hold stays at the peak.
        meter.update(level: level(forDB: -60), clipRun: 0, at: 1.99)
        XCTAssertEqual(meter.holdFraction, fraction(ofDB: -6), accuracy: 1e-4,
                       "the hold sits at the peak for the dwell")
        // Past the dwell now: two steps 0.1 s apart fall 10 dB/s * 0.1 s = 1 dB each.
        meter.update(level: level(forDB: -60), clipRun: 0, at: 2.09)
        let afterFirstFall = decibels(ofFraction: meter.holdFraction)
        meter.update(level: level(forDB: -60), clipRun: 0, at: 2.19)
        let afterSecondFall = decibels(ofFraction: meter.holdFraction)
        XCTAssertEqual(afterFirstFall - afterSecondFall, 1.0, accuracy: 0.05,
                       "the hold falls at 10 dB/s once the dwell expires")
        XCTAssertLessThan(afterSecondFall, -6, "the hold has fallen below the old peak")
        // A louder bar overtakes the falling hold line.
        meter.update(level: level(forDB: -6), clipRun: 0, at: 2.29)
        XCTAssertEqual(meter.holdFraction, meter.barFraction, accuracy: 1e-5,
                       "a louder bar lifts the hold to meet it")
        XCTAssertEqual(meter.holdFraction, fraction(ofDB: -6), accuracy: 1e-4)
    }

    // MARK: - Clip mark

    func testTwoConsecutiveFullScaleSamplesDoNotLightTheMarkButThreeDo() {
        var meter = LevelMeterState()
        meter.update(level: 1.0, clipRun: 2, at: 0)
        XCTAssertFalse(meter.clipLit, "two consecutive full-scale samples are a click, not clipping")
        meter.update(level: 1.0, clipRun: 3, at: 0.1)
        XCTAssertTrue(meter.clipLit, "three consecutive full-scale samples are sustained clipping")
    }

    func testTheClipMarkHoldsTwoSecondsThenIsAbsent() {
        var meter = LevelMeterState()
        meter.update(level: 1.0, clipRun: 3, at: 0.1)   // clipping chunk; held until 2.1
        XCTAssertTrue(meter.clipLit)
        meter.update(level: level(forDB: -6), clipRun: 0, at: 2.0)
        XCTAssertTrue(meter.clipLit, "still lit within the two-second hold")
        meter.update(level: level(forDB: -6), clipRun: 0, at: 2.1)
        XCTAssertFalse(meter.clipLit, "absent once the hold has elapsed")
    }

    func testResetReturnsToSilence() {
        var meter = LevelMeterState()
        meter.update(level: 1.0, clipRun: 5, at: 0)
        meter.reset()
        XCTAssertEqual(meter.barFraction, 0)
        XCTAssertEqual(meter.holdFraction, 0)
        XCTAssertFalse(meter.clipLit)
    }
}
