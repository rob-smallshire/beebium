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

/// The Econet station row's three states (#172): equal (settled), pending a
/// Break, and converged back to settled.
final class EconetStationPresentationTests: XCTestCase {

    func testEqualNumbersAreSettled() {
        let p = EconetStationPresentation(configured: 80, inForce: 80)
        XCTAssertEqual(p.state, .settled(station: 80))
        XCTAssertFalse(p.isPending)
        XCTAssertEqual(p.inForceText, "80")
        XCTAssertEqual(p.pendingSuffixText, "")
        XCTAssertNil(p.breakTooltip)
    }

    func testDifferingNumbersArePending() {
        // 80 in force, 83 configured: the machine is still using 80 and will
        // adopt 83 at the next Break.
        let p = EconetStationPresentation(configured: 83, inForce: 80)
        XCTAssertEqual(p.state, .pending(inForce: 80, configured: 83))
        XCTAssertTrue(p.isPending)
        XCTAssertEqual(p.inForceText, "80")
        XCTAssertEqual(p.pendingSuffixText, "\u{2192} 83")
        XCTAssertEqual(p.breakTooltip,
                       "83 takes effect at the next Break. Click to press Break.")
    }

    func testConvergedAfterBreakIsSettledAgain() {
        // The guest adopted 83: in force and configured now agree, so the
        // pending element disappears.
        let p = EconetStationPresentation(configured: 83, inForce: 83)
        XCTAssertEqual(p.state, .settled(station: 83))
        XCTAssertFalse(p.isPending)
        XCTAssertEqual(p.inForceText, "83")
    }

    func testZeroInForceIsTreatedAsSettled() {
        // Before the first report, or from a server too old to carry the field,
        // in force is zero; show the configured number alone, never "N -> 0".
        let p = EconetStationPresentation(configured: 80, inForce: 0)
        XCTAssertEqual(p.state, .settled(station: 80))
        XCTAssertFalse(p.isPending)
        XCTAssertEqual(p.inForceText, "80")
    }
}
