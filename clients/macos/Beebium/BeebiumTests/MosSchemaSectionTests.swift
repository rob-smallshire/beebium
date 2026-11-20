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

/// Decoding the `mos_image_uri` describe-preset-schema section into the model's
/// default MOS ROM that the Memory section's MOS field presents.
final class MosSchemaSectionTests: XCTestCase {

    private func decode(_ json: String) throws -> MosSchemaSection {
        try JSONDecoder().decode(MosSchemaSection.self, from: Data(json.utf8))
    }

    func testDecodesTheServerSection() throws {
        // The section as the Master 128 server publishes it.
        let section = try decode("""
        {
          "type": "mos_image_uri",
          "label": "MOS ROM",
          "default": "acorn-mos_3_20.rom",
          "description": "The MOS ROM: a ROM name (found on the ROM search path) or a path"
        }
        """)
        XCTAssertEqual(section.type, "mos_image_uri")
        XCTAssertEqual(section.label, "MOS ROM")
        XCTAssertEqual(section.default, "acorn-mos_3_20.rom")
        XCTAssertTrue(section.description.contains("ROM search path"))
    }

    func testToleratesMissingLabelAndDescription() throws {
        // Only `type` and `default` carry meaning; the rest must tolerate absence
        // so an older/leaner section still decodes.
        let section = try decode("""
        { "type": "mos_image_uri", "default": "acorn-mos_1_20.rom" }
        """)
        XCTAssertEqual(section.label, "MOS ROM")
        XCTAssertEqual(section.default, "acorn-mos_1_20.rom")
        XCTAssertEqual(section.description, "")
    }
}
