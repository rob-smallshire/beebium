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
            releaseDate: "1981-12",
            configuration: [:]
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
            releaseDate: nil,
            configuration: [:]
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
            releaseDate: nil,
            configuration: [:]
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
            releaseDate: nil,
            configuration: [:]
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
            releaseDate: nil,
            configuration: [:]
        )

        var set = Set<MachinePreset>()
        set.insert(preset)
        XCTAssertTrue(set.contains(preset))
    }
}

final class PresetFileDataTests: XCTestCase {

    func testDecodeMinimalPreset() throws {
        let json = """
        {
            "model": "model-b"
        }
        """

        let data = json.data(using: .utf8)!
        let preset = try JSONDecoder().decode(PresetFileData.self, from: data)

        XCTAssertEqual(preset.model, "model-b")
        XCTAssertNil(preset.name)
        XCTAssertNil(preset.machineName)
        XCTAssertNil(preset.description)
        XCTAssertNil(preset.releaseDate)
    }

    func testDecodeMachineNameTemplate() throws {
        let json = """
        {
            "model": "model-b",
            "name": "Station 80 (AUN, Model B)",
            "machine_name": "Station {econet-station} (AUN, Model B)"
        }
        """

        let data = json.data(using: .utf8)!
        let preset = try JSONDecoder().decode(PresetFileData.self, from: data)

        XCTAssertEqual(preset.machineName, "Station {econet-station} (AUN, Model B)")
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

        let data = json.data(using: .utf8)!
        let preset = try JSONDecoder().decode(PresetFileData.self, from: data)

        XCTAssertEqual(preset.model, "model-b")
        XCTAssertEqual(preset.name, "BBC Model B with Acorn DFS")
        XCTAssertEqual(preset.description, "Model B with Acorn 1770 FDC")
        XCTAssertEqual(preset.releaseDate, "1984-06")
    }

    func testDecodeYearOnlyReleaseDate() throws {
        let json = """
        {
            "model": "model-b-romram",
            "release_date": "1984"
        }
        """

        let data = json.data(using: .utf8)!
        let preset = try JSONDecoder().decode(PresetFileData.self, from: data)

        XCTAssertEqual(preset.model, "model-b-romram")
        XCTAssertEqual(preset.releaseDate, "1984")
    }
}
