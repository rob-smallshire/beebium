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

/// Tests the parsing and formatting of an Econet station value, and that the
/// three forms round-trip through the draft without a form being pinned to a
/// number.
final class EconetStationTests: XCTestCase {

    func testParsesTheThreeForms() {
        XCTAssertEqual(EconetStation(.int(80)), .number(80))
        XCTAssertEqual(EconetStation(.string("auto")), .auto)
        XCTAssertEqual(EconetStation(.string("auto:1-253")), .autoRange(1, 253))
    }

    func testMalformedAutoRangeFallsBackToAuto() {
        XCTAssertEqual(EconetStation(.string("auto:garbage")), .auto)
        XCTAssertEqual(EconetStation(.string("auto:1")), .auto)
    }

    func testAbsentStationIsZeroNumber() {
        // Which the server then flags as out of range.
        XCTAssertEqual(EconetStation(nil), .number(0))
    }

    func testFormatsBackToJSON() {
        XCTAssertEqual(EconetStation.number(254).jsonValue, .int(254))
        XCTAssertEqual(EconetStation.auto.jsonValue, .string("auto"))
        XCTAssertEqual(EconetStation.autoRange(1, 253).jsonValue, .string("auto:1-253"))
    }

    func testRoundTrips() {
        for station: EconetStation in [.number(80), .auto, .autoRange(10, 20)] {
            XCTAssertEqual(EconetStation(station.jsonValue), station)
        }
    }

    func testAutoDoesNotPinANumber() {
        // The whole point: an "auto" station stays "auto" through the draft, not
        // the number a launch happened to pick.
        let draft = MachineDraft(model: "model-b",
                                 econet: MachineDraft.Econet(station: .string("auto")))
        XCTAssertEqual(EconetStation(draft.econet?.station), .auto)
        XCTAssertEqual(draft.econet?.station, .string("auto"))
    }
}
