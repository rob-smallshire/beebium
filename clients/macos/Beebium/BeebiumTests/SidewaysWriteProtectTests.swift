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

/// The pure mapping from a SetSlotWriteProtect RPC response to a state change or
/// an error message. Covers every branch the live path can take short of the
/// transport throwing (which the caller turns into its own error message).
@MainActor
final class SidewaysWriteProtectTests: XCTestCase {

    func testSuccessAppliesReportedState() {
        XCTAssertEqual(
            SidewaysClient.writeProtectOutcome(success: true, error: "", writeProtected: true, slot: 15),
            .applied(true))
        // The applied state is whatever the server reports, not what was asked:
        // a release reported as still-protected (or vice-versa) is honoured.
        XCTAssertEqual(
            SidewaysClient.writeProtectOutcome(success: true, error: "", writeProtected: false, slot: 15),
            .applied(false))
    }

    func testFailureWithMessageIsRejectedVerbatim() {
        XCTAssertEqual(
            SidewaysClient.writeProtectOutcome(success: false,
                                               error: "Slot 3 is not RAM",
                                               writeProtected: false, slot: 3),
            .rejected("Slot 3 is not RAM"))
    }

    func testFailureWithoutMessageGetsASlotSpecificFallback() {
        // The server should always set an error, but if it doesn't we must still
        // say something truthful and slot-specific rather than a silent no-op.
        XCTAssertEqual(
            SidewaysClient.writeProtectOutcome(success: false, error: "",
                                               writeProtected: false, slot: 7),
            .rejected("Could not change write-protect for slot 7."))
    }
}
