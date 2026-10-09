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
        {"key":"mode","label":"Mode","description":"'pty' or 'device'","type":"string","required":false,"is_list":false,"position":null,"default":"pty","min":null,"max":null,"choices":[{"value":"pty","label":"Pseudo-terminal"},{"value":"device","label":"Serial device"}],"bundled":"","unique":false,"provider_of":""}
        {"key":"path","label":"Path","description":"the serial device path","type":"string","required":false,"is_list":false,"position":null,"default":"","min":null,"max":null,"choices":[],"bundled":"","unique":false,"provider_of":""}
        {"key":"baud","label":"Baud","description":"line speed","type":"integer","required":false,"is_list":false,"position":null,"default":"19200","min":50,"max":115200,"choices":[],"bundled":"","unique":false,"provider_of":""}
        """
        let manifest = ExtensionManifest.parse(name: "host-serial", jsonl: jsonl)
        XCTAssertEqual(manifest.name, "host-serial")
        XCTAssertEqual(manifest.parameters.map(\.key), ["mode", "path", "baud"])
        XCTAssertEqual(manifest.parameters[0].displayLabel, "Mode")
        XCTAssertEqual(manifest.parameters[0].choices.map(\.value), ["pty", "device"])
        XCTAssertEqual(manifest.parameters[0].choices.first?.label, "Pseudo-terminal")
        XCTAssertEqual(manifest.parameters[2].type, "integer")
        XCTAssertEqual(manifest.parameters[2].minimum, 50)
        XCTAssertEqual(manifest.parameters[2].maximum, 115200)
    }

    func testOrdersByPosition() {
        let jsonl = """
        {"key":"b","label":"B","description":"","type":"string","required":false,"is_list":false,"position":1,"default":"","min":null,"max":null,"choices":[],"bundled":"","unique":false,"provider_of":""}
        {"key":"a","label":"A","description":"","type":"string","required":false,"is_list":false,"position":0,"default":"","min":null,"max":null,"choices":[],"bundled":"","unique":false,"provider_of":""}
        """
        let manifest = ExtensionManifest.parse(name: "x", jsonl: jsonl)
        XCTAssertEqual(manifest.parameters.map(\.key), ["a", "b"])
    }

    func testParsesScsiHardDiscHints() {
        let jsonl = """
        {"key":"scsi-id","label":"SCSI ID","description":"SCSI target ID","type":"integer","required":false,"is_list":false,"position":0,"default":"0","min":0,"max":7,"choices":[],"bundled":"","unique":true,"provider_of":""}
        {"key":"image","label":"Image","description":"disc image","type":"filepath","required":false,"is_list":false,"position":1,"default":"","min":null,"max":null,"choices":[],"bundled":"hard-disc","unique":false,"provider_of":""}
        {"key":"adapter-id","label":"Adapter","description":"adapter to attach to","type":"string","required":false,"is_list":false,"position":2,"default":"","min":null,"max":null,"choices":[],"bundled":"","unique":false,"provider_of":"scsi"}
        """
        let manifest = ExtensionManifest.parse(name: "scsi-hard-disc", jsonl: jsonl)
        let byKey = Dictionary(uniqueKeysWithValues: manifest.parameters.map { ($0.key, $0) })
        XCTAssertEqual(byKey["scsi-id"]?.unique, true)
        XCTAssertEqual(byKey["scsi-id"]?.maximum, 7)
        XCTAssertEqual(byKey["image"]?.bundled, "hard-disc")
        XCTAssertEqual(byKey["image"]?.type, "filepath")
        XCTAssertEqual(byKey["adapter-id"]?.providerOf, "scsi")
    }

    func testSkipsMalformedLines() {
        let jsonl = """
        {"key":"ok","label":"OK","type":"string","is_list":false,"required":false,"default":"","bundled":"","unique":false,"provider_of":"","description":"","choices":[]}
        not json
        """
        let manifest = ExtensionManifest.parse(name: "x", jsonl: jsonl)
        XCTAssertEqual(manifest.parameters.map(\.key), ["ok"])
    }
}
