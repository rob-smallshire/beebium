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

final class MachinePresetTests: XCTestCase {

    func testSystemPresetIsNotEditable() {
        let preset = MachinePreset(
            id: UUID(),
            presetId: "bbc-model-b",
            name: "BBC Model B",
            coreExecutablePath: "/path/to/beebium-model-b",
            presetFilepath: "/path/to/presets/bbc-model-b.preset.beebium",
            source: .systemPreset,
            machineName: nil,
            modelName: "BBC Model B",
            modelDescription: "The original BBC Micro",
            releaseDate: "1981-12"
        )

        XCTAssertTrue(preset.isSystemProvided)
        XCTAssertFalse(preset.isEditable)
    }

    func testUserPresetIsEditable() {
        let preset = MachinePreset(
            id: UUID(),
            presetId: "my-custom-setup",
            name: "My Custom Setup",
            coreExecutablePath: "/path/to/beebium-model-b",
            presetFilepath: "/path/to/user/presets/my-custom-setup.preset.beebium",
            source: .userPreset,
            machineName: nil,
            modelName: "BBC Model B",
            modelDescription: nil,
            releaseDate: nil
        )

        XCTAssertFalse(preset.isSystemProvided)
        XCTAssertTrue(preset.isEditable)
    }

    func testPresetEquality() {
        let id = UUID()
        let preset1 = MachinePreset(
            id: id,
            presetId: "bbc-model-b",
            name: "BBC Model B",
            coreExecutablePath: "/path/to/beebium-model-b",
            presetFilepath: "/path/to/presets/bbc-model-b.preset.beebium",
            source: .systemPreset,
            machineName: nil,
            modelName: "BBC Model B",
            modelDescription: nil,
            releaseDate: nil
        )
        let preset2 = MachinePreset(
            id: id,
            presetId: "different-preset",
            name: "Different Name",
            coreExecutablePath: "/different/path",
            presetFilepath: "/different/path/preset.preset.beebium",
            source: .userPreset,
            machineName: nil,
            modelName: "Different Model",
            modelDescription: nil,
            releaseDate: nil
        )

        XCTAssertEqual(preset1, preset2)
    }

    func testPresetHashable() {
        let id = UUID()
        let preset = MachinePreset(
            id: id,
            presetId: "bbc-model-b",
            name: "BBC Model B",
            coreExecutablePath: "/path/to/beebium-model-b",
            presetFilepath: "/path/to/presets/bbc-model-b.preset.beebium",
            source: .systemPreset,
            machineName: nil,
            modelName: "BBC Model B",
            modelDescription: nil,
            releaseDate: nil
        )

        var set = Set<MachinePreset>()
        set.insert(preset)
        XCTAssertTrue(set.contains(preset))
    }
}

/// The discovery metadata read from a preset file comes from decoding a
/// MachineDraft (the single preset representation); these cover the fields
/// discovery relies on.
final class PresetDiscoveryMetadataTests: XCTestCase {

    func testDecodeMinimalPreset() throws {
        let draft = try MachineDraft.decode(from: Data(#"{ "model": "model-b" }"#.utf8))
        XCTAssertEqual(draft.model, "model-b")
        XCTAssertNil(draft.name)
        XCTAssertNil(draft.machineName)
        XCTAssertNil(draft.description)
        XCTAssertNil(draft.releaseDate)
    }

    func testDecodeMachineNameTemplate() throws {
        let json = """
        {
            "model": "model-b",
            "name": "Station 80 (AUN, Model B)",
            "machine_name": "Station {econet-station} (AUN, Model B)"
        }
        """
        let draft = try MachineDraft.decode(from: Data(json.utf8))
        XCTAssertEqual(draft.machineName, "Station {econet-station} (AUN, Model B)")
    }

    func testDecodeFullPreset() throws {
        let json = """
        {
            "model": "model-b",
            "name": "BBC Model B with Acorn DFS",
            "description": "Model B with Acorn 1770 FDC",
            "release_date": "1984-06"
        }
        """
        let draft = try MachineDraft.decode(from: Data(json.utf8))
        XCTAssertEqual(draft.model, "model-b")
        XCTAssertEqual(draft.name, "BBC Model B with Acorn DFS")
        XCTAssertEqual(draft.description, "Model B with Acorn 1770 FDC")
        XCTAssertEqual(draft.releaseDate, "1984-06")
    }

    func testDecodeYearOnlyReleaseDate() throws {
        let json = #"{ "model": "model-b-romram", "release_date": "1984" }"#
        let draft = try MachineDraft.decode(from: Data(json.utf8))
        XCTAssertEqual(draft.model, "model-b-romram")
        XCTAssertEqual(draft.releaseDate, "1984")
    }
}
