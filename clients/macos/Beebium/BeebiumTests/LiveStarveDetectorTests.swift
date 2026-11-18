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

/// Tests for the live-input starve detector's state machine and the copyable
/// count figures. Pure: every case is a sequence of per-second observations,
/// with no clock, view or server.
final class LiveStarveDetectorTests: XCTestCase {

    /// Feed a sequence of `(overruns, underruns)` seconds and return the
    /// condition raised at each, so a case reads as its own timeline.
    private func run(_ detector: inout LiveStarveDetector,
                     _ seconds: [(UInt64, UInt64)]) -> [LiveStarveCondition?] {
        seconds.map { detector.observe(overruns: $0.0, underruns: $0.1) }
    }

    func testTheConstantIsThree() {
        XCTAssertEqual(LiveStarveDetector.starveSeconds, 3,
                       "LIVE_STARVE_SECONDS is three consecutive growing seconds")
    }

    func testFirstObservationIsBaselineOnly() {
        var detector = LiveStarveDetector()
        // A large first count is just the starting point, never a raise.
        XCTAssertNil(detector.observe(overruns: 9999, underruns: 9999))
    }

    func testThreeGrowingSecondsRaiseAnUnderrun() {
        var detector = LiveStarveDetector()
        let raised = run(&detector, [(0, 0), (0, 1), (0, 2), (0, 3)])
        XCTAssertEqual(raised, [nil, nil, nil, .underrun])
    }

    func testThreeGrowingSecondsRaiseAnOverrun() {
        var detector = LiveStarveDetector()
        let raised = run(&detector, [(0, 0), (1, 0), (2, 0), (3, 0)])
        XCTAssertEqual(raised, [nil, nil, nil, .overrun])
    }

    func testTwoGrowingSecondsDoNotRaise() {
        var detector = LiveStarveDetector()
        let raised = run(&detector, [(0, 0), (0, 1), (0, 2)])
        XCTAssertEqual(raised, [nil, nil, nil], "two growing seconds are not enough")
    }

    func testAGapResetsTheRun() {
        var detector = LiveStarveDetector()
        // Grow twice, then a flat second, then grow three: only the last run
        // of three raises.
        let raised = run(&detector, [(0, 0), (0, 1), (0, 2), (0, 2), (0, 3), (0, 4), (0, 5)])
        XCTAssertEqual(raised, [nil, nil, nil, nil, nil, nil, .underrun])
    }

    func testPersistentGrowthRaisesOnlyOnce() {
        var detector = LiveStarveDetector()
        let raised = run(&detector, [(0, 0), (0, 1), (0, 2), (0, 3), (0, 4), (0, 5), (0, 6)])
        XCTAssertEqual(raised, [nil, nil, nil, .underrun, nil, nil, nil],
                       "a drift that keeps growing is reported once, not every few seconds")
    }

    func testReRaisesOnlyAfterClearingForThreeSecondsThenRecurring() {
        var detector = LiveStarveDetector()
        let raised = run(&detector, [
            (0, 0),                   // baseline
            (0, 1), (0, 2), (0, 3),   // raise
            (0, 3), (0, 3),           // two flat seconds: not yet re-armed
            (0, 4), (0, 5), (0, 6),   // grows again, but still suppressed
            (0, 6), (0, 6), (0, 6),   // three flat seconds: re-armed
            (0, 7), (0, 8), (0, 9),   // grows afresh
        ])
        XCTAssertEqual(raised, [
            nil,
            nil, nil, .underrun,
            nil, nil,
            nil, nil, nil,
            nil, nil, nil,
            nil, nil, .underrun,
        ])
    }

    func testTheOtherConditionCanRaiseWhileTheFirstIsShowing() {
        var detector = LiveStarveDetector()
        let raised = run(&detector, [
            (0, 0),                   // baseline
            (0, 1), (0, 2), (0, 3),   // underrun raises
            (1, 3), (2, 3), (3, 3),   // overrun now raises (underruns held flat)
        ])
        XCTAssertEqual(raised, [nil, nil, nil, .underrun, nil, nil, .overrun],
                       "the two conditions are independent, so the latest kind can replace the first")
    }

    func testBothGrowingTheSameSecondPrefersTheUnderrun() {
        var detector = LiveStarveDetector()
        let raised = run(&detector, [(0, 0), (1, 1), (2, 2), (3, 3)])
        XCTAssertEqual(raised.last, .underrun,
                       "made-up silence is the audible corruption, so it wins a tie")
    }

    func testResetStartsANewStreamClean() {
        var detector = LiveStarveDetector()
        _ = run(&detector, [(0, 0), (0, 1), (0, 2)])   // two growing seconds
        detector.reset()
        // After a reset the first observation is a fresh baseline, so a single
        // further growing second cannot complete a run begun before the reset.
        let raised = run(&detector, [(0, 50), (0, 51), (0, 52), (0, 53)])
        XCTAssertEqual(raised, [nil, nil, nil, .underrun])
    }

    // MARK: - Detail figures

    func testGroupedInsertsThousandsSeparators() {
        XCTAssertEqual(LiveStarveDetail.grouped(0), "0")
        XCTAssertEqual(LiveStarveDetail.grouped(999), "999")
        XCTAssertEqual(LiveStarveDetail.grouped(1204), "1,204")
        XCTAssertEqual(LiveStarveDetail.grouped(1_000_000), "1,000,000")
    }

    func testOverrunCountsAreDroppedSamples() {
        let text = LiveStarveDetail.counts(condition: .overrun, overruns: 1204,
                                           underruns: 0, sampleRate: 48000)
        XCTAssertEqual(text, "Dropped 1,204 samples since the stream began.")
    }

    func testUnderrunCountsAreSecondsOfSilenceFromTheRate() {
        // 38,400 samples at 48 kHz is 0.8 s.
        let text = LiveStarveDetail.counts(condition: .underrun, overruns: 0,
                                           underruns: 38400, sampleRate: 48000)
        XCTAssertEqual(text, "Made up 0.8 s of silence since the stream began.")
    }

    func testDetailTextLeadsWithTheSentence() {
        let text = LiveStarveDetail.text(condition: .underrun, overruns: 0,
                                         underruns: 38400, sampleRate: 48000)
        XCTAssertTrue(text.hasPrefix(LiveStarveCondition.underrun.sentence))
        XCTAssertTrue(text.hasSuffix("Made up 0.8 s of silence since the stream began."))
    }
}
