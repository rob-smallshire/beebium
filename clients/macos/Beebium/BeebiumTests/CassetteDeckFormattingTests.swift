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

/// The pure counter formatting and transport phrasing for the cassette deck.
final class CassetteDeckFormattingTests: XCTestCase {

    // MARK: - Counter

    func testCounterFormatsMinutesAndSeconds() {
        XCTAssertEqual(CassettePosition.format(0), "0:00")
        XCTAssertEqual(CassettePosition.format(5), "0:05")
        XCTAssertEqual(CassettePosition.format(65), "1:05")
        XCTAssertEqual(CassettePosition.format(90 * 60), "90:00")
    }

    func testCounterFloorsSecondsLikeAMechanicalCounter() {
        XCTAssertEqual(CassettePosition.format(12.9), "0:12")
        XCTAssertEqual(CassettePosition.format(59.99), "0:59")
    }

    func testCounterClampsNegativeAndNonFiniteToStart() {
        XCTAssertEqual(CassettePosition.format(-1), "0:00")
        XCTAssertEqual(CassettePosition.format(.nan), "0:00")
        XCTAssertEqual(CassettePosition.format(.infinity), "0:00")
    }

    // MARK: - Transport phrasing

    func testStoppedAndWindsNeverCarryTheMotor() {
        XCTAssertEqual(CassetteTransportText.label(transport: .stopped, motorOn: false), "Stopped")
        XCTAssertEqual(CassetteTransportText.label(transport: .stopped, motorOn: true), "Stopped")
        XCTAssertEqual(CassetteTransportText.label(transport: .rewinding, motorOn: false), "Rewinding")
        XCTAssertEqual(CassetteTransportText.label(transport: .fastForwarding, motorOn: false),
                       "Fast forwarding")
    }

    func testPlayingAndRecordingCarryMotorOffAsTheWaitingState() {
        XCTAssertEqual(CassetteTransportText.label(transport: .playing, motorOn: true), "Playing")
        XCTAssertEqual(CassetteTransportText.label(transport: .playing, motorOn: false),
                       "Playing, motor off")
        XCTAssertEqual(CassetteTransportText.label(transport: .recording, motorOn: true), "Recording")
        XCTAssertEqual(CassetteTransportText.label(transport: .recording, motorOn: false),
                       "Recording, motor off")
    }

    // MARK: - Position timeline geometry

    func testTimelinePointerSitsProportionallyAlongTheTape() {
        XCTAssertEqual(TapeTimelineGeometry.fraction(position: 0, duration: 300), 0)
        XCTAssertEqual(TapeTimelineGeometry.fraction(position: 150, duration: 300), 0.5)
        XCTAssertEqual(TapeTimelineGeometry.fraction(position: 300, duration: 300), 1)
    }

    func testTimelinePointerClampsPastTheEnd() {
        XCTAssertEqual(TapeTimelineGeometry.fraction(position: 400, duration: 300), 1)
    }

    func testTimelinePointerReadsTheStartWhenThereIsNoDuration() {
        XCTAssertEqual(TapeTimelineGeometry.fraction(position: 10, duration: 0), 0)
        XCTAssertEqual(TapeTimelineGeometry.fraction(position: 10, duration: -5), 0)
        XCTAssertEqual(TapeTimelineGeometry.fraction(position: -1, duration: 300), 0)
        XCTAssertEqual(TapeTimelineGeometry.fraction(position: .nan, duration: 300), 0)
        XCTAssertEqual(TapeTimelineGeometry.fraction(position: .infinity, duration: 300), 0)
    }

    func testTimelineScrubMapsAPointOnTheBarToAPosition() {
        XCTAssertEqual(TapeTimelineGeometry.position(atX: 0, width: 200, duration: 300), 0)
        XCTAssertEqual(TapeTimelineGeometry.position(atX: 100, width: 200, duration: 300), 150)
        XCTAssertEqual(TapeTimelineGeometry.position(atX: 200, width: 200, duration: 300), 300)
    }

    func testTimelineScrubClampsOutsideTheBar() {
        XCTAssertEqual(TapeTimelineGeometry.position(atX: -40, width: 200, duration: 300), 0)
        XCTAssertEqual(TapeTimelineGeometry.position(atX: 260, width: 200, duration: 300), 300)
    }

    func testTimelineScrubReadsTheStartForAnEmptyBar() {
        XCTAssertEqual(TapeTimelineGeometry.position(atX: 50, width: 0, duration: 300), 0)
        XCTAssertEqual(TapeTimelineGeometry.position(atX: 50, width: 200, duration: 0), 0)
    }

    // MARK: - Latching transport keys

    private func appearance(_ key: CassetteKey,
                            transport: Beebium_TapeTransport = .stopped,
                            isLive: Bool = false,
                            recordable: Bool = true) -> CassetteKeyAppearance {
        CassetteKeys.appearance(of: key, transport: transport,
                                isLive: isLive, recordable: recordable)
    }

    func testEmptyDeckShowsNoTransportRow() {
        // An empty deck omits the transport key row, the counter and the
        // timeline entirely -- there is no greyed row, so nothing comes alive
        // when a tape goes in. The row appears only with a tape (file or live).
        XCTAssertFalse(CassetteDeckLayout.showsTransport(tapeInserted: false),
                       "an empty deck must not show the transport row")
        XCTAssertTrue(CassetteDeckLayout.showsTransport(tapeInserted: true),
                      "a deck with a tape in must show the transport row")
    }

    func testStoppedTapeLeavesEveryKeyLiveAndNothingDown() {
        for key in CassetteKey.allCases {
            let a = appearance(key, transport: .stopped)
            XCTAssertTrue(a.enabled, "\(key) must stay live with a recordable tape in")
            XCTAssertFalse(a.depressed, "nothing is down while Stopped, including \(key)")
            XCTAssertFalse(a.red, "\(key) must not be red while Stopped")
        }
    }

    func testPlayingLatchesOnlyPlay() {
        XCTAssertEqual(appearance(.play, transport: .playing),
                       CassetteKeyAppearance(enabled: true, depressed: true, red: false))
        for key in [CassetteKey.stop, .rewind, .fastForward, .backToStart, .record] {
            XCTAssertFalse(appearance(key, transport: .playing).depressed,
                           "\(key) must not be down while Playing")
        }
        // Record stays live and ordinary-coloured while Playing, not red.
        XCTAssertEqual(appearance(.record, transport: .playing),
                       CassetteKeyAppearance(enabled: true, depressed: false, red: false))
    }

    func testRecordingLatchesRecordAndPlayTogetherWithRecordRed() {
        XCTAssertEqual(appearance(.record, transport: .recording),
                       CassetteKeyAppearance(enabled: true, depressed: true, red: true))
        // Record implies Play, so Play is down too -- but never red.
        XCTAssertEqual(appearance(.play, transport: .recording),
                       CassetteKeyAppearance(enabled: true, depressed: true, red: false))
        XCTAssertFalse(appearance(.stop, transport: .recording).depressed)
    }

    func testWindsLatchTheWindInProgress() {
        XCTAssertTrue(appearance(.rewind, transport: .rewinding).depressed)
        XCTAssertFalse(appearance(.fastForward, transport: .rewinding).depressed)
        XCTAssertTrue(appearance(.fastForward, transport: .fastForwarding).depressed)
        XCTAssertFalse(appearance(.rewind, transport: .fastForwarding).depressed)
        // A wind is never red.
        XCTAssertFalse(appearance(.rewind, transport: .rewinding).red)
    }

    func testNonRecordableTapeGreysRecordOnly() {
        XCTAssertEqual(appearance(.record, transport: .stopped, recordable: false),
                       CassetteKeyAppearance(enabled: false, depressed: false, red: false))
        XCTAssertTrue(appearance(.play, transport: .stopped, recordable: false).enabled)
        XCTAssertTrue(appearance(.stop, transport: .stopped, recordable: false).enabled)
    }

    func testLiveInputGreysWindsSeekAndRecordButNotPlayOrStop() {
        for key in [CassetteKey.rewind, .fastForward, .backToStart, .record] {
            XCTAssertFalse(appearance(key, transport: .playing, isLive: true, recordable: false).enabled,
                           "\(key) must be greyed on a live input")
        }
        XCTAssertTrue(appearance(.play, transport: .playing, isLive: true, recordable: false).enabled)
        XCTAssertTrue(appearance(.stop, transport: .playing, isLive: true, recordable: false).enabled)
        // A live input still latches Play while it is playing.
        XCTAssertTrue(appearance(.play, transport: .playing, isLive: true, recordable: false).depressed)
    }

    func testBackToTheBeginningStaysLiveEvenAtTheStart() {
        // It no longer greys at position 0: the ruling greys it only on a live
        // input, and the server's Seek(0) is harmless when already there.
        XCTAssertTrue(appearance(.backToStart, transport: .stopped).enabled)
        XCTAssertFalse(appearance(.backToStart, transport: .stopped).depressed)
    }
}
