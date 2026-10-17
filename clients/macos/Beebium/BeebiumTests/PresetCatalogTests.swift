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

/// Decoding the server's `list-presets --format jsonl` rows, and mapping them
/// to the system and user preset lists. Both the decode and the mapping are
/// pure seams, so they are pinned against samples here without running a
/// server. The sample lines are copied verbatim from `beebium-model-b
/// --format jsonl list-presets --all-models`.
final class PresetListingDecodeTests: XCTestCase {

    /// A system preset naming a model, with a thumbnail and no machine_name.
    func testDecodesPlainSystemRow() throws {
        let line = #"{"id":"model-b","name":"Model B","source":"system","model":"model-b","description":"32K Model B, no disc","release_date":"1981-12","filepath":"/presets/model-b.beebium-preset","thumbnail":true}"#
        let rows = CoreCLI.decodeListPresets(line)
        XCTAssertEqual(rows.count, 1)
        let row = try XCTUnwrap(rows.first)
        XCTAssertEqual(row.id, "model-b")
        XCTAssertEqual(row.name, "Model B")
        XCTAssertEqual(row.source, "system")
        XCTAssertEqual(row.model, "model-b")
        XCTAssertNil(row.machineName)
        XCTAssertEqual(row.description, "32K Model B, no disc")
        XCTAssertEqual(row.releaseDate, "1981-12")
        XCTAssertEqual(row.filepath, "/presets/model-b.beebium-preset")
        XCTAssertTrue(row.thumbnail)
    }

    /// machine_name is read from the snake_case key when present.
    func testDecodesMachineNameTemplate() throws {
        let line = #"{"id":"model-b-disc-aun-80","name":"Station 80 (AUN, Model B)","source":"system","model":"model-b","machine_name":"Station {econet-station} (AUN, Model B)","description":"Econet over AUN (UDP), ANFS + DFS","release_date":"1985","filepath":"/presets/model-b-disc-aun-80.beebium-preset","thumbnail":true}"#
        let row = try XCTUnwrap(CoreCLI.decodeListPresets(line).first)
        XCTAssertEqual(row.machineName, "Station {econet-station} (AUN, Model B)")
        XCTAssertEqual(row.source, "system")
    }

    /// A user preset: source "user", no thumbnail to write.
    func testDecodesUserRowWithoutThumbnail() throws {
        let line = #"{"id":"model-b-with-integra-b-disc-copy","name":"Model B with Integra-B (Disc) Copy","source":"user","model":"model-b-integra-b","description":"Model B, Integra-B board","release_date":"1988","filepath":"/Users/me/Library/Application Support/Beebium/presets/model-b-with-integra-b-disc-copy.beebium-preset","thumbnail":false}"#
        let row = try XCTUnwrap(CoreCLI.decodeListPresets(line).first)
        XCTAssertEqual(row.source, "user")
        XCTAssertEqual(row.model, "model-b-integra-b")
        XCTAssertFalse(row.thumbnail)
    }

    /// The optional keys are genuinely optional: a row that gives only the
    /// always-present keys decodes with the rest nil.
    func testDecodesRowWithNoOptionalKeys() throws {
        let line = #"{"id":"bare","name":"Bare","source":"system","filepath":"/presets/bare.beebium-preset","thumbnail":false}"#
        let row = try XCTUnwrap(CoreCLI.decodeListPresets(line).first)
        XCTAssertNil(row.model)
        XCTAssertNil(row.machineName)
        XCTAssertNil(row.description)
        XCTAssertNil(row.releaseDate)
    }

    /// Several rows across lines; blank lines and undecodable lines are skipped
    /// rather than aborting the whole listing.
    func testDecodesMultipleRowsSkippingNoise() {
        let text = """
        {"id":"model-b","name":"Model B","source":"system","model":"model-b","description":"d","release_date":"1981-12","filepath":"/p/model-b.beebium-preset","thumbnail":true}

        this is not json
        {"id":"model-b-disc","name":"Model B (Disc)","source":"system","model":"model-b","description":"d","release_date":"1982","filepath":"/p/model-b-disc.beebium-preset","thumbnail":true}
        """
        let rows = CoreCLI.decodeListPresets(text)
        XCTAssertEqual(rows.map(\.id), ["model-b", "model-b-disc"])
    }

    func testEmptyOutputGivesNoRows() {
        XCTAssertTrue(CoreCLI.decodeListPresets("").isEmpty)
        XCTAssertTrue(CoreCLI.decodeListPresets("\n\n").isEmpty)
    }
}

final class PresetCatalogBuildTests: XCTestCase {

    private func row(id: String, source: String, model: String?,
                     thumbnail: Bool = true, filepath: String? = nil) -> PresetListing {
        PresetListing(
            id: id, name: id.capitalized, source: source, model: model,
            machineName: nil, description: "desc of \(id)", releaseDate: "1982",
            filepath: filepath ?? "/presets/\(id).beebium-preset", thumbnail: thumbnail)
    }

    /// A row's model is paired with `beebium-<model>` under the servers
    /// directory, source splits system from user, and the row's fields carry
    /// through to the MachinePreset.
    func testMapsRowsAndSplitsBySource() {
        let rows = [
            row(id: "model-b", source: "system", model: "model-b"),
            row(id: "my-b-plus", source: "user", model: "model-b-plus"),
        ]
        let result = PresetCatalog.build(
            rows: rows,
            serversDirpath: "/srv",
            listingExecutablePath: "/srv/beebium-model-b",
            executableExists: { _ in true },
            modelDisplayName: { _ in "BBC Model B" })

        XCTAssertEqual(result.skipped, [])
        XCTAssertEqual(result.system.count, 1)
        XCTAssertEqual(result.user.count, 1)

        let sys = result.system[0]
        XCTAssertEqual(sys.presetId, "model-b")
        XCTAssertEqual(sys.coreExecutablePath, "/srv/beebium-model-b")
        XCTAssertEqual(sys.presetFilepath, "/presets/model-b.beebium-preset")
        XCTAssertEqual(sys.modelDescription, "desc of model-b")
        XCTAssertEqual(sys.modelName, "BBC Model B")
        XCTAssertEqual(sys.source, .systemPreset)

        let usr = result.user[0]
        XCTAssertEqual(usr.coreExecutablePath, "/srv/beebium-model-b-plus")
        XCTAssertEqual(usr.source, .userPreset)
    }

    /// A row that names no model is owned by the listing executable.
    func testRowWithNoModelUsesListingExecutable() {
        let rows = [row(id: "generic", source: "system", model: nil)]
        let result = PresetCatalog.build(
            rows: rows,
            serversDirpath: "/srv",
            listingExecutablePath: "/srv/beebium-model-b",
            executableExists: { _ in true },
            modelDisplayName: { _ in "" })
        XCTAssertEqual(result.system.first?.coreExecutablePath, "/srv/beebium-model-b")
    }

    /// A preset whose owning executable is not in this build is skipped, not
    /// shown as something the app cannot launch.
    func testSkipsRowWithMissingExecutable() {
        let rows = [
            row(id: "model-b", source: "system", model: "model-b"),
            row(id: "exotic", source: "system", model: "model-z"),
        ]
        let result = PresetCatalog.build(
            rows: rows,
            serversDirpath: "/srv",
            listingExecutablePath: "/srv/beebium-model-b",
            executableExists: { $0 == "/srv/beebium-model-b" },
            modelDisplayName: { _ in "" })
        XCTAssertEqual(result.system.map(\.presetId), ["model-b"])
        XCTAssertEqual(result.skipped, ["exotic"])
    }
}
