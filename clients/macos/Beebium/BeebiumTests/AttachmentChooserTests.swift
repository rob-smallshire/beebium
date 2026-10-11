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

/// Tests the attachment chooser's view model: the candidates it offers for a
/// point and the catalogue text each one shows (what it needs, what it provides).
/// Every string comes from the catalogue; nothing is spelled out per-extension.
final class AttachmentChooserTests: XCTestCase {

    /// A schema with a point whose candidates exercise each text path: an adapter
    /// that takes parameters and provides a point, and a plain extension that
    /// takes none and provides none.
    private func schema() throws -> ExtensionsSchemaSection {
        let json = """
        {
          "type": "extensions",
          "attachment_points": [
            {
              "id": "1mhz-bus",
              "extensions": ["acorn-scsi", "music-5000"],
              "display_name": "1 MHz Bus",
              "min_occupancy": 0
            },
            {
              "id": "scsi",
              "provided_by": ["acorn-scsi"],
              "extensions": ["scsi-hard-disc"],
              "display_name": "SCSI Bus",
              "min_occupancy": 0,
              "max_occupancy": 7
            }
          ],
          "catalogue": [
            {
              "name": "acorn-scsi",
              "display_name": "Acorn SCSI Host Adapter",
              "description": "An Acorn SCSI host adapter for the 1 MHz bus.",
              "kind": "peripheral",
              "attaches_to": ["1mhz-bus"],
              "provides": ["scsi"],
              "parameters": [
                {"key": "host", "type": "string", "label": "Host"},
                {"key": "port", "type": "integer", "label": "Port"}
              ]
            },
            {
              "name": "music-5000",
              "display_name": "Hybrid Music 5000",
              "description": "The Hybrid Music 5000 synthesiser.",
              "kind": "peripheral",
              "attaches_to": ["1mhz-bus"]
            }
          ]
        }
        """
        return try JSONDecoder().decode(ExtensionsSchemaSection.self, from: Data(json.utf8))
    }

    func testCandidatesSortedByDisplayName() throws {
        let candidates = AttachmentChooser.candidates(
            names: ["music-5000", "acorn-scsi"], schema: try schema())
        // Sorted by friendly name: "Acorn ..." before "Hybrid ...".
        XCTAssertEqual(candidates.map(\.name), ["acorn-scsi", "music-5000"])
        XCTAssertEqual(candidates.map(\.displayName),
                       ["Acorn SCSI Host Adapter", "Hybrid Music 5000"])
    }

    func testCandidateCarriesDescription() throws {
        let candidates = AttachmentChooser.candidates(names: ["acorn-scsi"], schema: try schema())
        XCTAssertEqual(candidates.first?.description,
                       "An Acorn SCSI host adapter for the 1 MHz bus.")
    }

    func testNeedsListsParameterLabels() throws {
        let schema = try schema()
        XCTAssertEqual(AttachmentChooser.needsText(schema.entry(forExtension: "acorn-scsi")),
                       "Host, Port")
    }

    func testNeedsEmptyWhenNoParameters() throws {
        let schema = try schema()
        XCTAssertEqual(AttachmentChooser.needsText(schema.entry(forExtension: "music-5000")), "")
    }

    func testProvidesUsesPointDisplayName() throws {
        let schema = try schema()
        XCTAssertEqual(
            AttachmentChooser.providesText(schema.entry(forExtension: "acorn-scsi"), schema: schema),
            "SCSI Bus")
    }

    func testProvidesEmptyWhenNothingProvided() throws {
        let schema = try schema()
        XCTAssertEqual(
            AttachmentChooser.providesText(schema.entry(forExtension: "music-5000"), schema: schema),
            "")
    }

    func testProvidesFallsBackToPointIdWhenUnnamed() throws {
        // An extension that provides a point the schema does not describe shows the
        // point's identifier rather than nothing.
        let json = """
        {
          "type": "extensions",
          "attachment_points": [
            {"id": "1mhz-bus", "extensions": ["gadget"], "min_occupancy": 0}
          ],
          "catalogue": [
            {"name": "gadget", "display_name": "Gadget", "provides": ["mystery-bus"]}
          ]
        }
        """
        let schema = try JSONDecoder().decode(ExtensionsSchemaSection.self, from: Data(json.utf8))
        XCTAssertEqual(
            AttachmentChooser.providesText(schema.entry(forExtension: "gadget"), schema: schema),
            "mystery-bus")
    }

    func testCandidatesFallBackToIdentifierWithoutCatalogue() throws {
        // An older server with no catalogue: the candidate still lists, named by
        // its identifier, with empty description/needs/provides.
        let json = """
        { "type": "extensions", "attachment_points": [] }
        """
        let schema = try JSONDecoder().decode(ExtensionsSchemaSection.self, from: Data(json.utf8))
        let candidates = AttachmentChooser.candidates(names: ["legacy-ext"], schema: schema)
        XCTAssertEqual(candidates.map(\.name), ["legacy-ext"])
        XCTAssertEqual(candidates.first?.displayName, "legacy-ext")
        XCTAssertEqual(candidates.first?.description, "")
        XCTAssertEqual(candidates.first?.needs, "")
        XCTAssertEqual(candidates.first?.provides, "")
    }
}
