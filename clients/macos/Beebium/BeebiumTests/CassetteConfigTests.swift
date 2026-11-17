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

/// Tests for the New Machine configurator's cassette support: the draft's
/// `storage.cassette.image_uri` round-trips losslessly, and the storage schema
/// decodes the `cassette` block the configurator gates its row on.
final class CassetteConfigTests: XCTestCase {

    // MARK: - Draft round-trip

    func testDraftRoundTripsStorageCassetteImageUri() throws {
        let json = """
        {
          "model": "model-b",
          "storage": {
            "fdc_socket": { "id": "acorn-1770" },
            "cassette": { "image_uri": "file:///tapes/Elite.uef" }
          }
        }
        """
        let first = try MachineDraft.decode(from: Data(json.utf8))
        let second = try MachineDraft.decode(from: try first.encoded())
        XCTAssertEqual(first, second, "round-trip changed the draft")
        XCTAssertEqual(first.storage?.cassette?.imageUri, "file:///tapes/Elite.uef")
    }

    func testDraftPreservesUnknownKeysUnderCassette() throws {
        // A key the model does not understand must survive a round-trip, so a
        // preset is not corrupted as the server grows cassette keys.
        let json = """
        {
          "model": "model-b",
          "storage": {
            "cassette": { "image_uri": "file:///tapes/x.uef", "future_key": 7 }
          }
        }
        """
        let first = try MachineDraft.decode(from: Data(json.utf8))
        let second = try MachineDraft.decode(from: try first.encoded())
        XCTAssertEqual(first, second, "an unknown cassette key must survive the round-trip")
    }

    func testCassetteImageUriBridgesBetweenPathAndUri() {
        // The configurator keeps the draft's file:// URI form but presents a
        // plain path to the row, as it does for a floppy.
        let uri = StorageSectionView.imageUri(fromPath: "/tapes/Chuckie.uef")
        XCTAssertTrue(uri.hasPrefix("file://"))
        XCTAssertEqual(StorageSectionView.path(fromImageUri: uri), "/tapes/Chuckie.uef")
    }

    // MARK: - Schema decoding

    func testSchemaDecodesTheCassetteBlock() throws {
        let json = """
        {
          "type": "storage",
          "builtin": { "cassette": true, "fdc": null },
          "cassette": {
            "has_cassette": true,
            "label": "Cassette",
            "image_types": ["uef", "csw", "wav"]
          }
        }
        """
        let schema = try JSONDecoder().decode(StorageSchemaSection.self, from: Data(json.utf8))
        XCTAssertEqual(schema.cassette?.hasCassette, true)
        XCTAssertEqual(schema.cassette?.label, "Cassette")
        XCTAssertEqual(schema.cassette?.imageTypes, ["uef", "csw", "wav"])
    }

    func testSchemaWithoutACassetteBlockLeavesItNil() throws {
        let json = """
        { "type": "storage", "builtin": { "cassette": false, "fdc": null } }
        """
        let schema = try JSONDecoder().decode(StorageSchemaSection.self, from: Data(json.utf8))
        XCTAssertNil(schema.cassette, "a model with no cassette block gets no configurator row")
    }
}
