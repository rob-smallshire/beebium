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

/// Tests for the `MachineDraft` preset document model.
///
/// The central guarantee is lossless round-tripping: decoding a preset,
/// re-encoding it and decoding again yields an equal `MachineDraft`, including
/// keys the model does not understand (which must survive so a passed-through
/// preset is not corrupted as the server grows keys).
final class MachineDraftTests: XCTestCase {

    // MARK: - Round-trip helpers

    /// Decode, re-encode, decode again, and assert the two drafts are equal.
    /// Returns the decoded draft for further field-level assertions.
    @discardableResult
    private func assertRoundTrips(_ json: String,
                                  _ label: String = "",
                                  file: StaticString = #filePath,
                                  line: UInt = #line) throws -> MachineDraft {
        let data = Data(json.utf8)
        let first = try MachineDraft.decode(from: data)
        let reencoded = try first.encoded()
        let second = try MachineDraft.decode(from: reencoded)
        XCTAssertEqual(first, second, "round-trip changed the draft \(label)", file: file, line: line)
        return first
    }

    // MARK: - Full-surface corpus

    func testRoundTripsFileServerPreset() throws {
        // A Level 3 File Server preset exercises every modelled area: a
        // machine_name template, auto_boot, storage with an FDC, sideways ROMs,
        // econet with a transport and its string parameters, and an extensions
        // array mixing bare entries with config objects (including an integer
        // value, scsi-id).
        let json = """
        {
          "model": "model-b",
          "name": "Station 254 (L3 File Server, Model B)",
          "release_date": "1985",
          "description": "Level 3 File Server",
          "machine_name": "L3 File Server, station {econet-station}",
          "auto_boot": true,
          "storage": { "fdc_socket": { "id": "acorn-1770" } },
          "sideways_bank": {
            "slots": [
              { "slot": 9, "type": "rom", "image_uri": "acorn-anfs_4_18.rom" },
              { "slot": 10, "type": "rom", "image_uri": "acorn-adfs_1_30.rom" }
            ]
          },
          "econet": {
            "station": 254,
            "transport": { "name": "aun", "parameters": { "discovery": "on", "net": "0", "port": "0" } }
          },
          "extensions": [
            { "name": "acorn-rtc", "config": { "layout": "7bit-year-in-r7" } },
            { "name": "acorn-scsi" },
            { "name": "scsi-hard-disc", "config": { "image": "l3fs-v1_26b.dat", "scsi-id": 0 } },
            { "name": "acorn-65c02-coprocessor" }
          ]
        }
        """
        let draft = try assertRoundTrips(json, "l3fs")

        XCTAssertEqual(draft.model, "model-b")
        XCTAssertEqual(draft.machineName, "L3 File Server, station {econet-station}")
        XCTAssertEqual(draft.autoBoot, true)
        XCTAssertEqual(draft.storage?.fdcSocket?.id, "acorn-1770")
        XCTAssertEqual(draft.sidewaysBank?.slots.count, 2)
        XCTAssertEqual(draft.econet?.station, .int(254))
        XCTAssertEqual(draft.econet?.transport?.name, "aun")
        XCTAssertEqual(draft.econet?.transport?.parameters?["port"], .string("0"))
        XCTAssertEqual(draft.extensions?.count, 4)

        // An integer config value stays an integer (not 0.0).
        let scsi = draft.extensions?.first { $0.name == "scsi-hard-disc" }
        XCTAssertEqual(scsi?.config?["scsi-id"], .int(0))
        XCTAssertEqual(scsi?.config?["image"], .string("l3fs-v1_26b.dat"))

        // A bare extension has no config.
        let copro = draft.extensions?.first { $0.name == "acorn-65c02-coprocessor" }
        XCTAssertNotNil(copro)
        XCTAssertNil(copro?.config)
    }

    func testRoundTripsStoragePreset() throws {
        // FDC socket, floppy drives (one mounted, one explicitly null).
        let json = """
        {
          "name": "Storage Test Preset",
          "model": "model-b",
          "storage": {
            "fdc_socket": { "id": "acorn-1770" },
            "floppy_drives": [
              { "drive": 0, "image_uri": "file:///tmp/test.ssd" },
              { "drive": 1, "image_uri": null }
            ]
          }
        }
        """
        let draft = try assertRoundTrips(json, "storage")

        XCTAssertEqual(draft.storage?.fdcSocket?.id, "acorn-1770")
        XCTAssertEqual(draft.storage?.floppyDrives?.count, 2)
        XCTAssertEqual(draft.storage?.floppyDrives?[0].imageUri, "file:///tmp/test.ssd")
        // An explicit null and an absent image_uri both mean "no disc".
        XCTAssertNil(draft.storage?.floppyDrives?[1].imageUri)
    }

    func testRoundTripsSidewaysKinds() throws {
        let json = """
        {
          "name": "Sideways Test Preset",
          "model": "model-b",
          "sideways_bank": {
            "slots": [
              { "slot": 14, "type": "rom", "image_uri": "acorn-dfs_2_26.rom" },
              { "slot": 13, "type": "empty" },
              { "slot": 4, "type": "ram" },
              { "slot": 5, "type": "ram", "image_uri": "preload.rom" }
            ]
          }
        }
        """
        let draft = try assertRoundTrips(json, "sideways")
        let slots = try XCTUnwrap(draft.sidewaysBank?.slots)
        XCTAssertEqual(slots.map(\.type), ["rom", "empty", "ram", "ram"])
        XCTAssertEqual(slots[0].imageUri, "acorn-dfs_2_26.rom")
        XCTAssertNil(slots[1].imageUri)
        XCTAssertNil(slots[2].imageUri)
        XCTAssertEqual(slots[3].imageUri, "preload.rom")
    }

    func testRoundTripsMinimalPreset() throws {
        // Only a name; no model. The draft represents what is present.
        let draft = try assertRoundTrips(#"{ "name": "Minimal Preset" }"#, "minimal")
        XCTAssertEqual(draft.name, "Minimal Preset")
        XCTAssertNil(draft.model)
        XCTAssertNil(draft.storage)
    }

    func testRoundTripsAutoStation() throws {
        // `--station auto` serialises as the string "auto" rather than an
        // integer, so the station field must preserve either form.
        let json = """
        {
          "model": "model-b",
          "econet": {
            "station": "auto",
            "transport": { "name": "aun", "parameters": { "net": "0" } }
          }
        }
        """
        let draft = try assertRoundTrips(json, "auto-station")
        XCTAssertEqual(draft.econet?.station, .string("auto"))
    }

    func testRoundTripsSpeedAndStartup() throws {
        let modeBoot = try assertRoundTrips("""
        { "model": "model-b", "speed": 1.5,
          "startup": { "screen_mode": 7, "auto_boot": true } }
        """, "speed+startup(mode)")
        XCTAssertEqual(modeBoot.speed, .double(1.5))
        XCTAssertEqual(modeBoot.startup?.screenMode, 7)
        XCTAssertEqual(modeBoot.startup?.autoBoot, true)
        XCTAssertNil(modeBoot.startup?.links)

        let unlimited = try assertRoundTrips("""
        { "model": "model-b", "speed": "unlimited", "startup": { "links": 128 } }
        """, "speed+startup(links)")
        XCTAssertEqual(unlimited.speed, .string("unlimited"))
        XCTAssertEqual(unlimited.startup?.links, 128)
        XCTAssertNil(unlimited.startup?.screenMode)
    }

    // MARK: - Unknown-key preservation

    func testPreservesUnknownKeys() throws {
        // Keys the model does not understand must survive a round-trip, at the
        // top level and nested inside a modelled object.
        let json = """
        {
          "name": "Unknown Keys Preset",
          "model": "model-b",
          "future_feature": { "some": "data", "count": 3 },
          "colour": "green",
          "storage": {
            "fdc_socket": { "id": "acorn-1770" },
            "unknown_storage_key": true
          }
        }
        """
        let draft = try assertRoundTrips(json, "unknown")

        XCTAssertEqual(draft.extra["colour"], .string("green"))
        XCTAssertEqual(draft.extra["future_feature"],
                       .object(["some": .string("data"), "count": .int(3)]))
        XCTAssertEqual(draft.storage?.extra["unknown_storage_key"], .bool(true))

        // The unknown keys must also appear in the re-encoded JSON.
        let reencoded = try draft.encoded()
        let object = try XCTUnwrap(
            try JSONSerialization.jsonObject(with: reencoded) as? [String: Any])
        XCTAssertNotNil(object["future_feature"])
        XCTAssertEqual(object["colour"] as? String, "green")
        let storage = try XCTUnwrap(object["storage"] as? [String: Any])
        XCTAssertEqual(storage["unknown_storage_key"] as? Bool, true)
    }

    // MARK: - JSONValue

    func testJSONValueKeepsIntegersAndDoublesDistinct() throws {
        let json = #"{ "model": "model-b", "extra_int": 7, "extra_double": 1.5 }"#
        let draft = try MachineDraft.decode(from: Data(json.utf8))
        XCTAssertEqual(draft.extra["extra_int"], .int(7))
        XCTAssertEqual(draft.extra["extra_double"], .double(1.5))
    }

    // MARK: - Every bundled preset (when a build tree is available)

    func testEveryBundledPresetRoundTrips() throws {
        guard let presetsDirpath = Self.bundledPresetsDirpath() else {
            throw XCTSkip("No build-tree presets directory found; the embedded corpus still covers the schema surface.")
        }
        let fm = FileManager.default
        let names = try fm.contentsOfDirectory(atPath: presetsDirpath)
            .filter { $0.hasSuffix(".preset.beebium") }
            .sorted()
        XCTAssertFalse(names.isEmpty, "presets directory \(presetsDirpath) held no preset files")

        for name in names {
            let filepath = "\(presetsDirpath)/\(name)"
            let data = try XCTUnwrap(fm.contents(atPath: filepath), "could not read \(name)")
            let first = try MachineDraft.decode(from: data)
            let second = try MachineDraft.decode(from: first.encoded())
            XCTAssertEqual(first, second, "round-trip changed bundled preset \(name)")
        }
        NSLog("[MachineDraftTests] round-tripped \(names.count) bundled preset(s) from \(presetsDirpath)")
    }

    /// Locate a build-tree `src/server/presets` directory holding generated
    /// presets, searching the build directories beside the repo root. Returns
    /// the one with the most preset files, or nil if none exists (a clean
    /// checkout with no build).
    private static func bundledPresetsDirpath() -> String? {
        let fm = FileManager.default
        var root = URL(fileURLWithPath: #filePath)
        // Walk up to the repository root (the directory that contains src/).
        for _ in 0..<10 {
            root.deleteLastPathComponent()
            if fm.fileExists(atPath: root.appendingPathComponent("src/server").path) {
                break
            }
        }
        guard let entries = try? fm.contentsOfDirectory(atPath: root.path) else { return nil }
        var best: (path: String, count: Int)?
        for entry in entries where entry.hasPrefix("build") || entry == "build" {
            let candidate = root
                .appendingPathComponent(entry)
                .appendingPathComponent("src/server/presets").path
            guard let files = try? fm.contentsOfDirectory(atPath: candidate) else { continue }
            let count = files.filter { $0.hasSuffix(".preset.beebium") }.count
            if count > 0, count > (best?.count ?? 0) {
                best = (candidate, count)
            }
        }
        return best?.path
    }
}
