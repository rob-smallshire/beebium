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

final class DiscDropRefusalTests: XCTestCase {
    private let occupied = "Eject disc first"

    func testAcceptsAnEmptyLocalSlot() {
        // What the file is does not enter into the hover decision -- only the
        // drive's state and the server's location, both known without it.
        XCTAssertNil(discDropRefusal(isSlotEmpty: true,
                                     isServerLocal: true,
                                     occupiedReason: occupied))
    }

    func testRefusesAnOccupiedSlot() {
        XCTAssertEqual(discDropRefusal(isSlotEmpty: false,
                                       isServerLocal: true,
                                       occupiedReason: occupied),
                       .slotOccupied(occupied))
    }

    func testRefusesWhenTheServerIsElsewhere() {
        // A path is only meaningful to a process sharing this filesystem.
        XCTAssertEqual(discDropRefusal(isSlotEmpty: true,
                                       isServerLocal: false,
                                       occupiedReason: occupied),
                       .serverElsewhere)
    }

    func testRemoteServerIsReportedAheadOfAnOccupiedSlot() {
        // Nothing local -- neither ejecting nor anything else -- can make a
        // drop onto a remote server work, so that is what to say.
        XCTAssertEqual(discDropRefusal(isSlotEmpty: false,
                                       isServerLocal: false,
                                       occupiedReason: occupied),
                       .serverElsewhere)
    }

    func testEveryRefusalCarriesAReason() {
        for refusal: DiscDropRefusal in [.slotOccupied(occupied), .serverElsewhere] {
            XCTAssertFalse(refusal.message.isEmpty)
        }
    }
}


final class HostFingerprintTests: XCTestCase {
    func testThisHostHasAFingerprint() {
        // macOS answers gethostuuid on an unsandboxed process, so a nil here
        // means the derivation broke, not that the platform declined.
        XCTAssertNotNil(HostFingerprint.current)
    }

    func testFingerprintIsASha256Digest() throws {
        let fingerprint = try XCTUnwrap(HostFingerprint.current)
        XCTAssertEqual(fingerprint.count, 64)
        XCTAssertTrue(fingerprint.allSatisfy { $0.isHexDigit && !$0.isUppercase })
    }

    func testFingerprintIsStable() {
        // Two processes on one host must agree, so it cannot vary per call.
        XCTAssertEqual(HostFingerprint.current, HostFingerprint.current)
    }

    func testThisHostMatchesItsOwnFingerprint() throws {
        let fingerprint = try XCTUnwrap(HostFingerprint.current)
        XCTAssertTrue(HostFingerprint.isThisHost(fingerprint))
    }

    func testAnotherHostDoesNotMatch() {
        XCTAssertFalse(HostFingerprint.isThisHost(String(repeating: "a", count: 64)))
    }

    func testUnknownIsNotAMatch() {
        // A server that will not say where it is must not be taken for a
        // local one: that would enable precisely the features that break.
        XCTAssertFalse(HostFingerprint.isThisHost(""))
    }
}
