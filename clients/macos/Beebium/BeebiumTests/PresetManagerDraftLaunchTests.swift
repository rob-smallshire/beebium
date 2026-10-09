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

/// Tests for the pure plumbing behind launching and saving a MachineDraft: the
/// `start` argument shape (server plumbing only, no per-setting overrides) and
/// serialising a draft to a temporary preset file.
final class PresetManagerDraftLaunchTests: XCTestCase {

    func testLaunchArgumentsAreServerPlumbingOnly() {
        let args = PresetManager.draftLaunchArguments(
            presetFilepath: "/tmp/x.preset.beebium",
            machineName: "Model B {machine-ordinal}",
            ordinal: 2,
            provenanceUUID: "UUID-1",
            romDirpath: nil)

        XCTAssertEqual(args, [
            "start",
            "--preset", "/tmp/x.preset.beebium",
            "--port", "0",
            "--advertise",
            "--wait=api",
            "--machine-name", "Model B {machine-ordinal}",
            "--machine-ordinal", "2",
            "--provenance-type", "macos-gui",
            "--provenance-uuid", "UUID-1",
        ])

        // The whole point of the rework: no per-setting CLI overrides -- the
        // preset document carries storage and sideways configuration.
        XCTAssertFalse(args.contains("--floppy"))
        XCTAssertFalse(args.contains("--sideways"))
        XCTAssertFalse(args.contains("--fdc"))
    }

    func testLaunchArgumentsAppendRomDirWhenBundled() {
        let args = PresetManager.draftLaunchArguments(
            presetFilepath: "/tmp/x.preset.beebium",
            machineName: "M {machine-ordinal}",
            ordinal: 1,
            provenanceUUID: "U",
            romDirpath: "/Applications/Beebium.app/roms")
        XCTAssertEqual(args.suffix(2), ["--rom-dir", "/Applications/Beebium.app/roms"])
    }

    func testWriteTemporaryPresetRoundTripsAndIsUnique() throws {
        let draft = MachineDraft(
            model: "model-b",
            name: "Scratch",
            storage: .init(fdcSocket: .init(id: "acorn-1770")),
            sidewaysBank: .init(slots: [
                .init(slot: 14, type: "rom", imageUri: "acorn-dfs_2_26.rom"),
            ]))

        let url1 = try PresetManager.writeTemporaryPreset(draft)
        defer { try? FileManager.default.removeItem(at: url1) }
        let url2 = try PresetManager.writeTemporaryPreset(draft)
        defer { try? FileManager.default.removeItem(at: url2) }

        XCTAssertNotEqual(url1, url2, "each call must write a distinct file")
        XCTAssertTrue(url1.lastPathComponent.hasSuffix(".preset.beebium"))
        XCTAssertTrue(FileManager.default.fileExists(atPath: url1.path))

        // The file on disk decodes back to an equal draft.
        let data = try XCTUnwrap(FileManager.default.contents(atPath: url1.path))
        XCTAssertEqual(try MachineDraft.decode(from: data), draft)
    }
}
