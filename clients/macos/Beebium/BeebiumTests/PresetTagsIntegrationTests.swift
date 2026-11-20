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

/// Real-catalogue integration test for the preset-filter spike. The unit tests
/// mirror the preset files with inline fixtures, so they cannot catch a break in
/// the path from a `list-presets` row to a tag -- which is exactly where the
/// first spike failed (the `.beebium-preset` files are ZIP bundles, read as raw
/// JSON, so every preset came back with no tags). This runs the real
/// `list-presets --all-models` from the build tree through the welcome window's
/// own code path (PresetFilterModel.index, which extracts preset.json from the
/// bundle) and asserts every preset is at least classified by family.
///
/// It spawns the server, so it is skipped unless BEEBIUM_PRESET_TAGS_INTEGRATION=1
/// (pass it as TEST_RUNNER_BEEBIUM_PRESET_TAGS_INTEGRATION=1 to xcodebuild, which
/// forwards it into the test host). Run (from clients/macos/Beebium):
///   TEST_RUNNER_BEEBIUM_PRESET_TAGS_INTEGRATION=1 xcodebuild test -scheme Beebium \
///     -configuration Debug -destination 'platform=macOS' \
///     -only-testing:BeebiumTests/PresetTagsIntegrationTests
@MainActor
final class PresetTagsIntegrationTests: XCTestCase {

    func testEveryBundledPresetGetsAtLeastAFamilyTag() async throws {
        guard ProcessInfo.processInfo.environment["BEEBIUM_PRESET_TAGS_INTEGRATION"] == "1" else {
            throw XCTSkip("set BEEBIUM_PRESET_TAGS_INTEGRATION=1 to run the preset-tags integration test")
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
                modelId: row.model, source: .systemPreset,
                machineDescription: row.machineDescription, modelName: "",
                modelDescription: row.description, releaseDate: row.releaseDate,
                hasThumbnail: row.thumbnail)
        }

        let filter = PresetFilterModel()
        await filter.index(presets)

        // Every preset must carry a model tag -- the one tag that comes from the
        // row's model, so it holds even if a bundle cannot be read.
        for preset in presets {
            let tags = filter.tags(forPresetId: preset.presetId)
            let models = tags.filter { $0.group == .model }
            XCTAssertEqual(models.count, 1,
                           "\(preset.presetId) should have exactly one Model tag, got \(tags.sorted())")
        }

        // The Master presets must read Master 128 -- proof the bundle is actually
        // parsed, not just the model prefix. There are three in the catalogue.
        let masters = presets.filter { $0.modelId == "master-128" }
        XCTAssertGreaterThanOrEqual(masters.count, 3, "expected at least three Master presets")
        for master in masters {
            let tags = filter.tags(forPresetId: master.presetId)
            XCTAssertTrue(tags.contains(PresetTag(group: .model, value: "Master 128")),
                          "\(master.presetId) should tag Master 128")
            // The Master's built-in WD1770 comes from its storage schema, so every
            // Master reads disc controller 1770 even with no storage block.
            XCTAssertTrue(tags.contains(PresetTag(group: .discController, value: "1770")),
                          "\(master.presetId) should tag disc controller 1770 from its built-in controller")
        }

        // And at least one preset gained a richer tag from the bundle (not just
        // model), proving the ZIP read works end to end -- e.g. a disc controller.
        let anyStorageTag = presets.contains { preset in
            filter.tags(forPresetId: preset.presetId).contains {
                $0.group == .discController && $0.value != "No disc"
            }
        }
        XCTAssertTrue(anyStorageTag, "no preset got a disc controller tag; the bundle read is probably failing")
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
