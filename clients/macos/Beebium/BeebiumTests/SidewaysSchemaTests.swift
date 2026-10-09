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

final class SidewaysSchemaTests: XCTestCase {

    func testDecodeModelBSidewaysSection() throws {
        // Shape emitted by describe-preset-schema for a stock Model B: four
        // aliased sockets, each answering four slot numbers; BASIC default.
        let json = """
        {
          "type": "sideways_bank",
          "has_aliasing": true,
          "sockets": [
            { "label": "IC52",  "slots": [0, 4, 8, 12],  "capabilities": ["rom", "ram", "empty"], "runtime_configurable": false },
            { "label": "IC101", "slots": [3, 7, 11, 15], "capabilities": ["rom", "ram", "empty"], "runtime_configurable": false }
          ],
          "default_roms": [
            { "slot": 15, "image": "bbc-basic_2.rom", "role": "language" }
          ]
        }
        """
        let section = try JSONDecoder().decode(SidewaysSchemaSection.self, from: Data(json.utf8))

        XCTAssertEqual(section.type, "sideways_bank")
        XCTAssertTrue(section.hasAliasing)
        XCTAssertEqual(section.sockets.count, 2)
        XCTAssertEqual(section.defaultRoms.count, 1)
        XCTAssertEqual(section.defaultRoms[0].image, "bbc-basic_2.rom")
        XCTAssertEqual(section.defaultRoms[0].role, "language")
    }

    func testSocketPriorityIsHighestSlot() throws {
        let socket = SidewaysSocketSchema(
            label: "IC100", slots: [2, 6, 10, 14],
            capabilities: ["rom", "ram", "empty"], runtimeConfigurable: false)

        // The MOS scans high to low, so the aliased socket's priority is 14.
        XCTAssertEqual(socket.priority, 14)
        XCTAssertEqual(socket.representativeSlot, 14)
        XCTAssertTrue(socket.supportsRam)
        XCTAssertTrue(socket.supportsEmpty)
    }

    // Preset-file sideways_bank parsing is covered by MachineDraftTests, which
    // round-trips the whole preset document; this file keeps the schema-section
    // (describe-preset-schema) tests.
}
