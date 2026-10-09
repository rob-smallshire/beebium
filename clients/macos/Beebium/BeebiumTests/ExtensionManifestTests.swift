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

/// Tests parsing an extension manifest from describe-extension JSONL output.
final class ExtensionManifestTests: XCTestCase {

    func testParsesHostSerialParameters() {
        // Real describe-extension --format jsonl output for host-serial.
        let jsonl = """
        {"key":"mode","type":"string","is_list":false,"required":false,"default":"pty","bundled":"","unique":false,"provider_of":"","description":"'pty' or 'device'"}
        {"key":"path","type":"string","is_list":false,"required":false,"default":"","bundled":"","unique":false,"provider_of":"","description":"the serial device path"}
        {"key":"baud","type":"integer","is_list":false,"required":false,"default":"19200","bundled":"","unique":false,"provider_of":"","description":"line speed"}
        """
        let manifest = ExtensionManifest.parse(name: "host-serial", jsonl: jsonl)
        XCTAssertEqual(manifest.name, "host-serial")
        XCTAssertEqual(manifest.parameters.map(\.key), ["mode", "path", "baud"])
        XCTAssertEqual(manifest.parameters[2].type, "integer")
        XCTAssertEqual(manifest.parameters[2].defaultValue, "19200")
        // Richer properties are absent until describe-extension --json lands.
        XCTAssertNil(manifest.parameters[0].choices)
        XCTAssertNil(manifest.parameters[0].displayKind)
        // No explicit label yet, so the control falls back to the key.
        XCTAssertEqual(manifest.parameters[0].displayLabel, "mode")
    }

    func testParsesScsiHardDiscHints() {
        let jsonl = """
        {"key":"scsi-id","type":"integer","is_list":false,"required":false,"default":"0","bundled":"","unique":true,"provider_of":"","description":"SCSI target ID"}
        {"key":"image","type":"filepath","is_list":false,"required":false,"default":"","bundled":"hard-disc","unique":false,"provider_of":"","description":"disc image"}
        {"key":"adapter-id","type":"string","is_list":false,"required":false,"default":"","bundled":"","unique":false,"provider_of":"scsi","description":"adapter to attach to"}
        """
        let manifest = ExtensionManifest.parse(name: "scsi-hard-disc", jsonl: jsonl)
        let byKey = Dictionary(uniqueKeysWithValues: manifest.parameters.map { ($0.key, $0) })
        XCTAssertEqual(byKey["scsi-id"]?.unique, true)
        XCTAssertEqual(byKey["image"]?.bundled, "hard-disc")
        XCTAssertEqual(byKey["image"]?.type, "filepath")
        XCTAssertEqual(byKey["adapter-id"]?.providerOf, "scsi")
    }

    func testSkipsMalformedLines() {
        let jsonl = """
        {"key":"ok","type":"string","is_list":false,"required":false,"default":"","bundled":"","unique":false,"provider_of":"","description":""}
        not json
        """
        let manifest = ExtensionManifest.parse(name: "x", jsonl: jsonl)
        XCTAssertEqual(manifest.parameters.map(\.key), ["ok"])
    }

    func testDecodesRicherPropertiesWhenPresent() throws {
        // The describe-extension --json shape to come (min/max/choices/label/
        // display_kind); the model decodes them when present.
        let json = #"{"key":"baud","type":"integer","is_list":false,"required":true,"default":"19200","bundled":"","unique":false,"provider_of":"","description":"line speed","label":"Baud rate","min":50,"max":115200,"choices":["9600","19200"],"display_kind":"device"}"#
        let parameter = try JSONDecoder().decode(ExtensionParameter.self, from: Data(json.utf8))
        XCTAssertEqual(parameter.displayLabel, "Baud rate")
        XCTAssertEqual(parameter.minimum, 50)
        XCTAssertEqual(parameter.maximum, 115200)
        XCTAssertEqual(parameter.choices, ["9600", "19200"])
        XCTAssertEqual(parameter.displayKind, "device")
        XCTAssertTrue(parameter.required)
    }
}
