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

/// The failure kind -> inline marker label mapping (and its fallback), plus the
/// describe-disc-image token -> kind mapping the pre-launch path shares, and the
/// invariant that the popover always shows the verbatim server message.
final class DiscErrorKindLabelTests: XCTestCase {
    func testEveryKnownKindMapsToItsLabel() {
        XCTAssertEqual(Beebium_DiscErrorKind.cannotOpen.markerLabel, "Cannot open")
        XCTAssertEqual(Beebium_DiscErrorKind.empty.markerLabel, "Empty disc image")
        XCTAssertEqual(Beebium_DiscErrorKind.readError.markerLabel, "Read error")
        XCTAssertEqual(Beebium_DiscErrorKind.unrecognised.markerLabel, "Unrecognised format")
        XCTAssertEqual(Beebium_DiscErrorKind.loadFailed.markerLabel, "Couldn't load disc")
        XCTAssertEqual(Beebium_DiscErrorKind.noController.markerLabel, "No disc controller")
        XCTAssertEqual(Beebium_DiscErrorKind.invalidDrive.markerLabel, "Invalid drive")
        XCTAssertEqual(Beebium_DiscErrorKind.driveOccupied.markerLabel, "Drive occupied")
    }

    func testUnspecifiedAndUnknownFallBackToCallerLabel() {
        // nil means "use the caller's own context label" -- so an unclassified
        // kind (and any future kind we don't map yet) still reads honestly as
        // "Insert failed" / "Eject failed" / etc.
        XCTAssertNil(Beebium_DiscErrorKind.unspecified.markerLabel)
        XCTAssertNil(Beebium_DiscErrorKind.UNRECOGNIZED(99).markerLabel)
    }

    func testEveryKnownKindHasALabelViaAllCases() {
        // Guards against a new enum case being added without a label decision:
        // every case except the two fallbacks must map to a non-nil label.
        for kind in Beebium_DiscErrorKind.allCases where kind != .unspecified {
            XCTAssertNotNil(kind.markerLabel, "\(kind) needs a marker label or an explicit fallback")
        }
    }

    func testDescribeTokenMapsToKind() {
        XCTAssertEqual(Beebium_DiscErrorKind(describeToken: "cannot_open"), .cannotOpen)
        XCTAssertEqual(Beebium_DiscErrorKind(describeToken: "empty"), .empty)
        XCTAssertEqual(Beebium_DiscErrorKind(describeToken: "read_error"), .readError)
        XCTAssertEqual(Beebium_DiscErrorKind(describeToken: "unrecognised"), .unrecognised)
        XCTAssertEqual(Beebium_DiscErrorKind(describeToken: "load_failed"), .loadFailed)
        XCTAssertEqual(Beebium_DiscErrorKind(describeToken: "none"), .unspecified)
        XCTAssertEqual(Beebium_DiscErrorKind(describeToken: "something_new"), .unspecified)
    }

    func testPopoverDetailIsTheVerbatimServerMessage() {
        // The kind chooses the short label; the detail (popover) is unchanged
        // verbatim server text. Model the DiscError the client builds.
        let error = DiscError.operationFailed("Cannot open disc image: /Users/x/pic%20a.png",
                                              kind: .cannotOpen)
        XCTAssertEqual(error.kind.markerLabel, "Cannot open")
        XCTAssertEqual(error.errorDescription, "Cannot open disc image: /Users/x/pic%20a.png",
                       "the popover shows the server's verbatim message, not the short label")
    }

    func testNotConnectedIsUnclassified() {
        XCTAssertEqual(DiscError.notConnected.kind, .unspecified)
    }
}
