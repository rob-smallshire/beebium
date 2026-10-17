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
/// `start` argument shapes (server plumbing only, no per-setting overrides) for
/// both launching a preset file and opening a saved machine directory, and
/// serialising a draft to a temporary preset file.
final class PresetManagerDraftLaunchTests: XCTestCase {

    func testPresetLaunchArgumentsAreServerPlumbingOnly() {
        let args = PresetManager.presetLaunchArguments(
            presetFilepath: "/tmp/x.beebium-preset",
            ordinal: 2,
            provenanceUUID: "UUID-1",
            romDirpath: nil)

        XCTAssertEqual(args, [
            "start",
            "--preset", "/tmp/x.beebium-preset",
            "--port", "0",
            "--advertise",
            "--wait=api",
            "--machine-ordinal", "2",
            "--provenance-type", "macos-gui",
            "--provenance-uuid", "UUID-1",
        ])

        // The name is the server's to compose from the preset's own name plus
        // this ordinal, so no --machine-name is passed; the preset carries any
        // machine_description, so no --machine-description either.
        XCTAssertFalse(args.contains("--machine-name"))
        XCTAssertFalse(args.contains("--machine-description"))

        // The whole point of the rework: no per-setting CLI overrides -- the
        // preset document carries storage and sideways configuration.
        XCTAssertFalse(args.contains("--floppy"))
        XCTAssertFalse(args.contains("--sideways"))
        XCTAssertFalse(args.contains("--fdc"))
    }

    func testPresetLaunchArgumentsAppendRomDirWhenBundled() {
        let args = PresetManager.presetLaunchArguments(
            presetFilepath: "/tmp/x.beebium-preset",
            ordinal: 1,
            provenanceUUID: "U",
            romDirpath: "/Applications/Beebium.app/roms")
        XCTAssertEqual(args.suffix(2), ["--rom-dir", "/Applications/Beebium.app/roms"])
    }

    func testMachineLaunchArgumentsOpenASavedDirectory() {
        let args = PresetManager.machineLaunchArguments(
            machineDirpath: "/Users/me/Documents/Elite Server.beebium-machine",
            provenanceUUID: "UUID-9",
            romDirpath: nil)

        XCTAssertEqual(args, [
            "start",
            "--machine", "/Users/me/Documents/Elite Server.beebium-machine",
            "--port", "0",
            "--advertise",
            "--wait=api",
            "--provenance-type", "macos-gui",
            "--provenance-uuid", "UUID-9",
        ])

        // A saved machine carries its own name and identity in machine.json,
        // which the server reads and reports; the app supplies only plumbing,
        // never a preset, a name or an ordinal.
        XCTAssertFalse(args.contains("--preset"))
        XCTAssertFalse(args.contains("--machine-name"))
        XCTAssertFalse(args.contains("--machine-ordinal"))
    }

    func testMachineLaunchArgumentsAppendRomDirWhenBundled() {
        let args = PresetManager.machineLaunchArguments(
            machineDirpath: "/m/x.beebium-machine",
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
        XCTAssertTrue(url1.lastPathComponent.hasSuffix(".json"))
        XCTAssertTrue(FileManager.default.fileExists(atPath: url1.path))

        // The file on disk decodes back to an equal draft.
        let data = try XCTUnwrap(FileManager.default.contents(atPath: url1.path))
        XCTAssertEqual(try MachineDraft.decode(from: data), draft)
    }

    // MARK: - CoreCLI resolve-preset output decoding (pinned samples)

    func testDecodeResolveOutputParsesPresetAndDiagnostics() throws {
        let json = """
        {
          "preset": { "model": "model-b", "name": "Resolved" },
          "diagnostics": [
            { "severity": "warning", "path": "/storage/floppies/0",
              "message": "No disc image", "location": "Floppy drive 0" }
          ]
        }
        """
        let (preset, diagnostics) = try XCTUnwrap(CoreCLI.decodeResolveOutput(json))
        XCTAssertEqual(preset?.model, "model-b")
        XCTAssertEqual(preset?.name, "Resolved")
        XCTAssertEqual(diagnostics, [
            PresetManager.PresetDiagnostic(
                severity: "warning", path: "/storage/floppies/0",
                message: "No disc image", location: "Floppy drive 0"),
        ])
    }

    func testDecodeResolveOutputAllowsNullPreset() throws {
        // resolve-preset prints diagnostics even when the document could not be
        // turned into a preset (a hard error); the preset is then null.
        let json = """
        { "preset": null,
          "diagnostics": [
            { "severity": "error", "path": "", "message": "unknown model", "location": "" }
          ] }
        """
        let (preset, diagnostics) = try XCTUnwrap(CoreCLI.decodeResolveOutput(json))
        XCTAssertNil(preset)
        XCTAssertEqual(diagnostics.count, 1)
        XCTAssertTrue(diagnostics.first?.isError ?? false)
    }

    func testDecodeResolveOutputReturnsNilForNonJSON() {
        XCTAssertNil(CoreCLI.decodeResolveOutput("not json at all"))
        XCTAssertNil(CoreCLI.decodeResolveOutput(""))
    }
}
