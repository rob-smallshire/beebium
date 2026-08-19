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

/// The client half of the describe-disc-image contract. The server half is
/// covered by the C++ describe-disc-image tests; these pin that the JSON it
/// emits decodes onto DiscImageInfo -- so a CodingKeys slip (write_protected)
/// or a shape change is caught here rather than at a drop.
final class DiscImageInfoTests: XCTestCase {
    private func decode(_ json: String) throws -> DiscImageInfo {
        try JSONDecoder().decode(DiscImageInfo.self, from: Data(json.utf8))
    }

    func testDecodesARecognisedImage() throws {
        // Exactly what `describe-disc-image` prints for a valid SSD.
        let info = try decode("""
            { "recognised": true, "format": "SSD", "sides": 1, "write_protected": false }
            """)
        XCTAssertTrue(info.recognised)
        XCTAssertEqual(info.format, "SSD")
        XCTAssertEqual(info.sides, 1)
        XCTAssertEqual(info.writeProtected, false)
        XCTAssertNil(info.reason)
    }

    func testDecodesADoubleSidedWriteProtectedImage() throws {
        let info = try decode("""
            { "recognised": true, "format": "DSD", "sides": 2, "write_protected": true }
            """)
        XCTAssertEqual(info.sides, 2)
        XCTAssertEqual(info.writeProtected, true)
    }

    func testDecodesAnUnrecognisedFile() throws {
        // For a non-disc file the size/write fields are absent and a reason
        // is present; the optionals must absorb that.
        let info = try decode("""
            { "recognised": false, "format": "", "reason": "Unrecognised disc image format (size=42, ext=.md): x" }
            """)
        XCTAssertFalse(info.recognised)
        XCTAssertEqual(info.format, "")
        XCTAssertNil(info.sides)
        XCTAssertNil(info.writeProtected)
        XCTAssertEqual(info.reason, "Unrecognised disc image format (size=42, ext=.md): x")
    }
}
