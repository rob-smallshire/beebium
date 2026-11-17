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
}
