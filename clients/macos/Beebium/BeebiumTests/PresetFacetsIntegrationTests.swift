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

import Foundation
import XCTest
@testable import Beebium

/// Real-catalogue integration test for the preset search. The unit tests use
/// inline fixtures, so they cannot catch a break in the path from a real
/// `list-presets` row's `facets` object to a search hit. This runs the real
/// `list-presets --all-models` from the build tree, maps the rows exactly as the
/// welcome window does, and checks the server's facets reach the search: every
/// row carries the six facets, and real queries find the presets a person means.
///
/// It spawns the server, so it is skipped unless BEEBIUM_PRESET_FACETS_INTEGRATION=1
/// (pass it as TEST_RUNNER_BEEBIUM_PRESET_FACETS_INTEGRATION=1 to xcodebuild, which
/// forwards it into the test host). Run (from clients/macos/Beebium):
///   TEST_RUNNER_BEEBIUM_PRESET_FACETS_INTEGRATION=1 xcodebuild test -scheme Beebium \
///     -configuration Debug -destination 'platform=macOS' \
///     -only-testing:BeebiumTests/PresetFacetsIntegrationTests
@MainActor
final class PresetFacetsIntegrationTests: XCTestCase {

    private static let expectedFacetKeys: Set<String> =
        ["model", "coprocessor", "disc_controller", "network", "storage", "peripherals"]

    func testRowsCarrySixFacetsAndRealQueriesFindTheirPresets() async throws {
        guard ProcessInfo.processInfo.environment["BEEBIUM_PRESET_FACETS_INTEGRATION"] == "1" else {
            throw XCTSkip("set BEEBIUM_PRESET_FACETS_INTEGRATION=1 to run the preset-facets integration test")
        }
        guard let serverPath = Self.findServer() else {
            throw XCTSkip("beebium-master-128 executable not found")
        }

        let rows = try Self.listPresets(serverPath: serverPath)
        XCTAssertFalse(rows.isEmpty, "list-presets returned no rows")

        // Map rows to presets exactly as PresetCatalog does for the welcome window.
        let presets = rows.map { row in
            MachinePreset(
                id: UUID(), presetId: row.id, name: row.name,
                coreExecutablePath: serverPath, presetFilepath: row.filepath,
                modelId: row.model, facets: row.facets?.byKey ?? [:],
                keywords: row.keywords ?? [],
                source: row.source == "user" ? .userPreset : .systemPreset,
                machineDescription: row.machineDescription, modelName: "",
                modelDescription: row.description, releaseDate: row.releaseDate,
                hasThumbnail: row.thumbnail)
        }

        // Every row carries all six facets.
        for preset in presets {
            XCTAssertEqual(Set(preset.facets.keys), Self.expectedFacetKeys,
                           "\(preset.presetId) should carry the six facets, has \(Set(preset.facets.keys))")
        }

        let filter = PresetFilterModel()
        filter.index(presets)

        // "l3" finds the Level 3 file server.
        filter.searchText = "l3"
        let l3 = filter.filtered(presets).map(\.presetId)
        XCTAssertTrue(l3.contains("model-b-l3fs-aun"), "'l3' should find the file server, found \(l3)")

        // "65c102 master" finds exactly the Master 65C102 copro preset (among the
        // built-in presets; a user's saved machines are not asserted on).
        filter.searchText = "65c102 master"
        let coproHits = filter.filtered(presets).filter(\.isSystemProvided).map(\.presetId)
        XCTAssertEqual(coproHits, ["master-128-65c102-copro"])

        // "6502 copro" must find the coprocessor presets via the server's
        // keywords -- "6502" is in no spelling of a 65C02/65C102 facet value. It
        // finds the Model B-family coprocessor presets and the Master's, and only
        // coprocessor presets (every system match has a coprocessor fitted).
        filter.searchText = "6502 copro"
        let sixFiveOhTwo = filter.filtered(presets).filter(\.isSystemProvided)
        let ids = Set(sixFiveOhTwo.map(\.presetId))
        XCTAssertTrue(ids.contains("master-128-65c102-copro"), "should find the Master copro, found \(ids)")
        XCTAssertTrue(ids.contains("model-b-disc-65c02-copro"), "should find the Model B 65C02 copro, found \(ids)")
        XCTAssertTrue(ids.contains("model-b-plus-128k-65c102-copro"), "should find the B+ 65C102 copro, found \(ids)")
        for preset in sixFiveOhTwo {
            XCTAssertNotEqual(preset.facets["coprocessor"], ["None"],
                              "\(preset.presetId) matched '6502 copro' but has no coprocessor")
        }
    }

    // MARK: - Helpers

    /// Run `list-presets --all-models --format jsonl` and decode its rows.
    private static func listPresets(serverPath: String) throws -> [PresetListing] {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: serverPath)
        process.arguments = ["--format", "jsonl", "list-presets", "--all-models"]
        let pipe = Pipe()
        process.standardOutput = pipe
        process.standardError = FileHandle.nullDevice
        try process.run()
        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        process.waitUntilExit()
        let decoder = JSONDecoder()
        return String(decoding: data, as: UTF8.self)
            .split(separator: "\n")
            .compactMap { line in
                try? decoder.decode(PresetListing.self, from: Data(line.utf8))
            }
    }

    private static func findServer() -> String? {
        if let frameworks = Bundle.main.privateFrameworksPath {
            let path = "\(frameworks)/beebium-master-128"
            if FileManager.default.isExecutableFile(atPath: path) { return path }
        }
        let home = FileManager.default.homeDirectoryForCurrentUser
        let build = home.appendingPathComponent("Code/beebium/build/src/server/beebium-master-128").path
        if FileManager.default.isExecutableFile(atPath: build) { return build }
        return nil
    }
}
