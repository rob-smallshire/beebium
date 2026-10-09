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
        // Index by key, never by position: a short list must fail an assertion,
        // not crash the test host with an out-of-range subscript.
        let byKey = Dictionary(uniqueKeysWithValues: manifest.parameters.map { ($0.key, $0) })
        XCTAssertEqual(byKey["mode"]?.displayLabel, "Mode")
        XCTAssertEqual(byKey["mode"]?.choices.map(\.value), ["pty", "device"])
        XCTAssertEqual(byKey["mode"]?.choices.first?.label, "Pseudo-terminal")
        XCTAssertEqual(byKey["baud"]?.type, "integer")
        XCTAssertEqual(byKey["baud"]?.minimum, 50)
        XCTAssertEqual(byKey["baud"]?.maximum, 115200)
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

    func testParsesExcludesAndChoicesFrom() {
        let jsonl = """
        {"key":"time","label":"Time","description":"","type":"string","required":false,"is_list":false,"position":0,"default":"","min":null,"max":null,"choices":[],"excludes":["offset"],"choices_from":"","bundled":"","unique":false,"provider_of":""}
        {"key":"path","label":"Path","description":"","type":"string","required":false,"is_list":false,"position":1,"default":"","min":null,"max":null,"choices":[],"excludes":[],"choices_from":"serial-ports","bundled":"","unique":false,"provider_of":""}
        """
        let manifest = ExtensionManifest.parse(name: "x", jsonl: jsonl)
        let byKey = Dictionary(uniqueKeysWithValues: manifest.parameters.map { ($0.key, $0) })
        XCTAssertEqual(byKey["time"]?.excludes, ["offset"])
        XCTAssertEqual(byKey["path"]?.choicesFrom, "serial-ports")
        XCTAssertEqual(byKey["path"]?.excludes, [])
    }

    func testBundledImageAvailabilityByExtension() {
        let shared = BundledImage(image: "hd.dat", sizeBytes: 1, extensionName: nil)
        let owned = BundledImage(image: "rom.rom", sizeBytes: 1, extensionName: "acorn-65c102-coprocessor")
        XCTAssertTrue(shared.availableTo(extensionName: "anything"))
        XCTAssertTrue(owned.availableTo(extensionName: "acorn-65c102-coprocessor"))
        XCTAssertFalse(owned.availableTo(extensionName: "acorn-65c02-coprocessor"))
    }

    func testParsesSerialPortPaths() {
        let jsonl = """
        {"path":"/dev/cu.usbserial-1","usb_vendor_id":1027,"usb_product_id":24577,"manufacturer":"FTDI","product":"","serial_number":""}
        {"path":"/dev/cu.Bluetooth-Incoming-Port","usb_vendor_id":null,"usb_product_id":null,"manufacturer":"","product":"","serial_number":""}
        """
        XCTAssertEqual(PresetManager.parseSerialPortPaths(fromJSONL: jsonl),
                       ["/dev/cu.usbserial-1", "/dev/cu.Bluetooth-Incoming-Port"])
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
