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
                            motorOn: Bool = false,
                            isLive: Bool = false,
                            recordable: Bool = true,
                            appendRefusal: String = "") -> CassetteKeyAppearance {
        CassetteKeys.appearance(of: key, transport: transport, motorOn: motorOn,
                                isLive: isLive, recordable: recordable, appendRefusal: appendRefusal)
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

    func testTransportRowIsInTapeGeographyOrder() {
        // One group at the left edge, left to right in the tape's geography;
        // Eject is not a transport key and sits apart at the right edge.
        XCTAssertEqual(CassetteDeckLayout.transportRow,
                       [.backToStart, .rewind, .stop, .play, .record, .fastForward, .toEnd],
                       "the deck row runs in the tape's geography")
    }

    func testStoppedTapeLeavesEveryKeyLiveNothingDownAndUncoloured() {
        for motorOn in [false, true] {
            for key in CassetteKey.allCases {
                let a = appearance(key, transport: .stopped, motorOn: motorOn)
                XCTAssertTrue(a.enabled, "\(key) must stay live with a recordable tape in")
                XCTAssertFalse(a.depressed, "nothing is down while Stopped, including \(key)")
                XCTAssertEqual(a.colour, .none,
                               "Stopped never colours \(key), motor \(motorOn)")
            }
        }
    }

    func testPlayingColoursOnlyPlayAndByTheMotorRelay() {
        // Down but grey while the relay is open (the "Playing, motor off" wait).
        XCTAssertEqual(appearance(.play, transport: .playing, motorOn: false),
                       CassetteKeyAppearance(enabled: true, depressed: true, colour: .grey))
        // Green once the relay closes and the tape moves.
        XCTAssertEqual(appearance(.play, transport: .playing, motorOn: true),
                       CassetteKeyAppearance(enabled: true, depressed: true, colour: .green))
        for key in [CassetteKey.stop, .rewind, .fastForward, .backToStart, .record] {
            XCTAssertFalse(appearance(key, transport: .playing, motorOn: true).depressed,
                           "\(key) must not be down while Playing")
        }
        // Record is not down, so it is plain -- never coloured while up.
        XCTAssertEqual(appearance(.record, transport: .playing, motorOn: true),
                       CassetteKeyAppearance(enabled: true, depressed: false, colour: .none))
    }

    func testRecordingColoursRecordAndPlayRedByTheMotorRelay() {
        // Relay closed: Record and Play both down and red (Record implies Play).
        XCTAssertEqual(appearance(.record, transport: .recording, motorOn: true),
                       CassetteKeyAppearance(enabled: true, depressed: true, colour: .red))
        XCTAssertEqual(appearance(.play, transport: .recording, motorOn: true),
                       CassetteKeyAppearance(enabled: true, depressed: true, colour: .red))
        // Relay open: both down but grey -- red never appears without the relay.
        XCTAssertEqual(appearance(.record, transport: .recording, motorOn: false),
                       CassetteKeyAppearance(enabled: true, depressed: true, colour: .grey))
        XCTAssertEqual(appearance(.play, transport: .recording, motorOn: false),
                       CassetteKeyAppearance(enabled: true, depressed: true, colour: .grey))
        XCTAssertFalse(appearance(.stop, transport: .recording, motorOn: true).depressed)
    }

    func testWindsAreBlueWhileWindingWhateverTheMotor() {
        // A wind moves the tape regardless of the relay, so it is blue either way.
        for motorOn in [false, true] {
            XCTAssertEqual(appearance(.rewind, transport: .rewinding, motorOn: motorOn),
                           CassetteKeyAppearance(enabled: true, depressed: true, colour: .blue))
            XCTAssertEqual(appearance(.fastForward, transport: .fastForwarding, motorOn: motorOn),
                           CassetteKeyAppearance(enabled: true, depressed: true, colour: .blue))
        }
        // The other wind is neither down nor coloured.
        XCTAssertEqual(appearance(.fastForward, transport: .rewinding),
                       CassetteKeyAppearance(enabled: true, depressed: false, colour: .none))
        XCTAssertFalse(appearance(.rewind, transport: .fastForwarding).depressed)
    }

    func testNonRecordableTapeGreysRecordOnly() {
        XCTAssertEqual(appearance(.record, transport: .stopped, recordable: false),
                       CassetteKeyAppearance(enabled: false, depressed: false, colour: .none))
        XCTAssertTrue(appearance(.play, transport: .stopped, recordable: false).enabled)
        XCTAssertTrue(appearance(.stop, transport: .stopped, recordable: false).enabled)
    }

    func testRecordIsGreyedPurelyByRecordable() {
        XCTAssertTrue(appearance(.record, transport: .stopped, recordable: true).enabled)
        XCTAssertFalse(appearance(.record, transport: .stopped, recordable: false).enabled)
        // A live input is non-recordable, so the flag alone greys it.
        XCTAssertFalse(appearance(.record, transport: .playing, isLive: true, recordable: false).enabled)
    }

    func testRecordHelpIsTheServerRefusalWhenNotRecordable() {
        XCTAssertEqual(CassetteKeys.recordHelp(recordable: true, refusal: "ignored"), "Record")
        let csw = "The tape game.csw is a CSW image, and Beebium records only on UEF tapes."
        XCTAssertEqual(CassetteKeys.recordHelp(recordable: false, refusal: csw), csw,
                       "a non-recordable tape shows the server's own sentence")
        XCTAssertEqual(CassetteKeys.recordHelp(recordable: false, refusal: "   "),
                       "This tape cannot be recorded onto",
                       "with no server sentence, a generic fallback")
    }

    func testToEndIsEnabledOnlyWhenTheAppendIsAllowed() {
        // An empty refusal means the server would accept the append seek: the
        // key is live, and (being momentary) never down or coloured.
        XCTAssertEqual(appearance(.toEnd, appendRefusal: ""),
                       CassetteKeyAppearance(enabled: true, depressed: false, colour: .none))
        // A non-empty refusal greys it (a live tape, a not-ready map, an at-end
        // tape -- the server decides, the client does not re-derive the reason).
        let notReady = "The tape's signal map is not ready yet; try again in a moment."
        XCTAssertEqual(appearance(.toEnd, appendRefusal: notReady),
                       CassetteKeyAppearance(enabled: false, depressed: false, colour: .none))
        // Never down or coloured whatever the transport or motor.
        let moving = appearance(.toEnd, transport: .recording, motorOn: true, appendRefusal: "")
        XCTAssertFalse(moving.depressed)
        XCTAssertEqual(moving.colour, .none)
    }

    func testAppendHelpIsTheServerRefusalWhenGreyed() {
        XCTAssertEqual(CassetteKeys.appendHelp(refusal: ""), "To the end of the recordings")
        let notReady = "The tape's signal map is not ready yet; try again in a moment."
        XCTAssertEqual(CassetteKeys.appendHelp(refusal: notReady), notReady,
                       "a refused append shows the server's own sentence")
        XCTAssertEqual(CassetteKeys.appendHelp(refusal: "   "), "To the end of the recordings",
                       "no refusal reads as allowed")
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

    // MARK: - Signal map reduction

    func testSignalMapLightsTheLoudSecondsColumns() {
        // Five seconds, two of them full signal, stretched across ten columns:
        // the two loud seconds light the middle four columns.
        let columns = CassetteSignalMap.litColumns(
            seconds: 5, levels: [0, 0, 255, 255, 0], width: 10)
        XCTAssertEqual(columns, [false, false, false, false, true,
                                 true, true, true, false, false])
    }

    func testSignalMapDownsamplesByTheMaximum() {
        // A C90's worth of seconds into 200 columns: a single loud second lights
        // exactly its own column, by the maximum-over-the-span rule.
        var levels = [UInt8](repeating: 0, count: 5400)
        levels[2700] = 255
        let columns = CassetteSignalMap.litColumns(seconds: 5400, levels: levels, width: 200)
        XCTAssertEqual(columns.count, 200)
        XCTAssertTrue(columns[100], "the column spanning the one loud second must light")
        XCTAssertEqual(columns.filter { $0 }.count, 1, "and no other column")
    }

    func testSignalMapThresholdIsTheAccentAt128() {
        XCTAssertEqual(CassetteSignalMap.litColumns(seconds: 1, levels: [127], width: 1), [false])
        XCTAssertEqual(CassetteSignalMap.litColumns(seconds: 1, levels: [128], width: 1), [true])
        XCTAssertEqual(CassetteSignalMap.litColumns(seconds: 1, levels: [255], width: 1), [true])
    }

    func testSignalMapEmptyOrUnsizedYieldsNoColumns() {
        XCTAssertTrue(CassetteSignalMap.litColumns(seconds: 0, levels: [], width: 10).isEmpty,
                      "a not-ready map (no seconds) yields no coloured columns")
        XCTAssertTrue(CassetteSignalMap.litColumns(seconds: 300, levels: [], width: 10).isEmpty,
                      "an empty level array yields no coloured columns")
        XCTAssertTrue(CassetteSignalMap.litColumns(seconds: 5, levels: [0, 0, 255, 255, 0],
                                                   width: 0).isEmpty,
                      "a zero-width bar yields no columns")
    }
}
