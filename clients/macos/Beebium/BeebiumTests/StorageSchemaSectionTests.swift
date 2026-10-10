// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

/// The Storage section shows floppy drives only when the model has an active
/// floppy disc controller. These pin that decision to the schema and draft,
/// matching the live Storage sidebar.
final class StorageSchemaSectionTests: XCTestCase {

    private func schema(from json: String) throws -> StorageSchemaSection {
        try JSONDecoder().decode(StorageSchemaSection.self, from: Data(json.utf8))
    }

    private func draft(fdcId: String?) -> MachineDraft {
        var draft = MachineDraft(model: "model-b")
        if let fdcId = fdcId {
            var storage = MachineDraft.Storage()
            storage.fdcSocket = MachineDraft.FdcSocket(id: fdcId)
            draft.storage = storage
        }
        return draft
    }

    /// A soldered-in controller (e.g. the B+) always has an active FDC.
    func testBuiltinControllerIsActive() throws {
        let schema = try schema(from: """
        { "type": "storage",
          "builtin": { "cassette": true, "fdc": { "device": "WD1770", "label": "Built-in WD1770" } },
          "floppy_drives": [ { "drive": 0, "tracks": [40, 80], "sides": 2, "image_types": ["ssd"] } ] }
        """)
        XCTAssertTrue(schema.hasActiveFdc(in: draft(fdcId: nil)))
    }

    /// An empty FDC socket means no active controller.
    func testEmptySocketIsNotActive() throws {
        let schema = try schema(from: modelBSocketSchema)
        XCTAssertFalse(schema.hasActiveFdc(in: draft(fdcId: "none")))
        // Default (no fdc_socket in the draft) is the empty socket too.
        XCTAssertFalse(schema.hasActiveFdc(in: draft(fdcId: nil)))
    }

    /// Fitting a real controller activates the FDC.
    func testFittedControllerIsActive() throws {
        let schema = try schema(from: modelBSocketSchema)
        XCTAssertTrue(schema.hasActiveFdc(in: draft(fdcId: "acorn-1770")))
    }

    private let modelBSocketSchema = """
    { "type": "storage",
      "builtin": { "cassette": true, "fdc": null },
      "fdc_socket": { "options": [
        { "id": "none", "label": "Empty", "device": null, "description": "No disc controller" },
        { "id": "acorn-1770", "label": "Acorn 1770", "device": "WD1770", "description": "Acorn controller" }
      ] },
      "floppy_drives": [ { "drive": 0, "tracks": [40, 80], "sides": 2, "image_types": ["ssd"] } ] }
    """
}
