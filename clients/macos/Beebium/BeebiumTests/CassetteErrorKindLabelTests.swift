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

/// The cassette error kind to short inline marker mapping. The popover always
/// shows the server's verbatim sentence; the marker is only the honest label,
/// and an unclassified kind keeps the caller's own generic (nil here).
final class CassetteErrorKindLabelTests: XCTestCase {

    func testEveryClassifiedKindHasAMarker() {
        XCTAssertEqual(Beebium_CassetteErrorKind.noCassetteInterface.markerLabel,
                       "No cassette interface")
        XCTAssertEqual(Beebium_CassetteErrorKind.cannotOpen.markerLabel, "Cannot open")
        XCTAssertEqual(Beebium_CassetteErrorKind.unreadable.markerLabel, "Unreadable tape")
        XCTAssertEqual(Beebium_CassetteErrorKind.unsupportedFormat.markerLabel, "Unsupported format")
        XCTAssertEqual(Beebium_CassetteErrorKind.invalidImage.markerLabel, "Invalid tape image")
        XCTAssertEqual(Beebium_CassetteErrorKind.deckOccupied.markerLabel, "Deck occupied")
        XCTAssertEqual(Beebium_CassetteErrorKind.deckEmpty.markerLabel, "Deck empty")
        XCTAssertEqual(Beebium_CassetteErrorKind.invalidPosition.markerLabel, "Invalid position")
        XCTAssertEqual(Beebium_CassetteErrorKind.readOnly.markerLabel, "Read-only tape")
    }

    func testUnclassifiedKindsKeepTheCallersGeneric() {
        XCTAssertNil(Beebium_CassetteErrorKind.unspecified.markerLabel)
        XCTAssertNil(Beebium_CassetteErrorKind.UNRECOGNIZED(99).markerLabel)
    }
}
