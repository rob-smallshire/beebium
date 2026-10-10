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

/// The Configurator reads an extension's parameters, friendly name, description
/// and bundled ROMs from the model's catalogue in describe-preset-schema -- no
/// per-extension round-trip. These cover decoding that catalogue and turning an
/// entry into the parameter manifest the forms are driven by.
final class ExtensionCatalogueTests: XCTestCase {

    private func schema(from json: String) throws -> ExtensionsSchemaSection {
        try JSONDecoder().decode(ExtensionsSchemaSection.self, from: Data(json.utf8))
    }

    private func byKey(_ manifest: ExtensionManifest) -> [String: ExtensionParameter] {
        Dictionary(uniqueKeysWithValues: manifest.parameters.map { ($0.key, $0) })
    }

    /// A catalogue entry's parameters decode with their every hint, and the
    /// manifest keeps them -- indexed by key, never by position.
    func testManifestFromCatalogueCarriesParameterHints() throws {
        let schema = try schema(from: """
        { "type": "extensions", "attachment_points": [], "catalogue": [
          { "name": "scsi-hard-disc", "display_name": "SCSI Hard Disc",
            "description": "A hard disc on the SCSI bus.", "kind": "peripheral",
            "parameters": [
              {"key":"scsi-id","label":"SCSI ID","type":"integer","position":0,"default":"0","min":0,"max":7,"unique":true},
              {"key":"image","label":"Image","type":"filepath","position":1,"bundled":"hard-disc"},
              {"key":"adapter-id","label":"Adapter","type":"string","position":2,"provider_of":"scsi"}
            ] }
        ] }
        """)
        let manifest = schema.manifest(forExtension: "scsi-hard-disc")
        XCTAssertEqual(manifest.parameters.map(\.key), ["scsi-id", "image", "adapter-id"])
        let p = byKey(manifest)
        XCTAssertEqual(p["scsi-id"]?.unique, true)
        XCTAssertEqual(p["scsi-id"]?.maximum, 7)
        XCTAssertEqual(p["image"]?.bundled, "hard-disc")
        XCTAssertEqual(p["image"]?.type, "filepath")
        XCTAssertEqual(p["adapter-id"]?.providerOf, "scsi")
    }

    /// The manifest orders parameters by declared position.
    func testManifestOrdersByPosition() throws {
        let schema = try schema(from: """
        { "type": "extensions", "attachment_points": [], "catalogue": [
          { "name": "x", "parameters": [
            {"key":"b","type":"string","position":1},
            {"key":"a","type":"string","position":0} ] }
        ] }
        """)
        XCTAssertEqual(schema.manifest(forExtension: "x").parameters.map(\.key), ["a", "b"])
    }

    /// Choices, excludes and choices_from survive the round trip.
    func testManifestCarriesChoicesExcludesAndChoicesFrom() throws {
        let schema = try schema(from: """
        { "type": "extensions", "attachment_points": [], "catalogue": [
          { "name": "acorn-rtc", "parameters": [
            {"key":"mode","type":"string","position":0,"choices":[{"value":"pty","label":"Pseudo-terminal"}]},
            {"key":"time","type":"string","position":1,"excludes":["offset"]},
            {"key":"path","type":"string","position":2,"choices_from":"serial-ports"} ] }
        ] }
        """)
        let p = byKey(schema.manifest(forExtension: "acorn-rtc"))
        XCTAssertEqual(p["mode"]?.choices.map(\.value), ["pty"])
        XCTAssertEqual(p["mode"]?.choices.first?.label, "Pseudo-terminal")
        XCTAssertEqual(p["time"]?.excludes, ["offset"])
        XCTAssertEqual(p["path"]?.choicesFrom, "serial-ports")
    }

    /// Friendly name and description come from the entry; an unknown extension
    /// falls back to its identifier and an empty description.
    func testDisplayNameAndDescription() throws {
        let schema = try schema(from: """
        { "type": "extensions", "attachment_points": [], "catalogue": [
          { "name": "acorn-rtc", "display_name": "Acorn Real-Time Clock",
            "description": "Keeps the date and time." }
        ] }
        """)
        XCTAssertEqual(schema.displayName(forExtension: "acorn-rtc"), "Acorn Real-Time Clock")
        XCTAssertEqual(schema.description(forExtension: "acorn-rtc"), "Keeps the date and time.")
        XCTAssertEqual(schema.displayName(forExtension: "unknown"), "unknown")
        XCTAssertEqual(schema.description(forExtension: "unknown"), "")
        XCTAssertTrue(schema.manifest(forExtension: "unknown").parameters.isEmpty)
    }

    /// A coprocessor's bundled ROMs decode, and a ROM shows its friendly name
    /// (falling back to the file stem when none is given).
    func testCatalogueRoms() throws {
        let schema = try schema(from: """
        { "type": "extensions", "attachment_points": [], "catalogue": [
          { "name": "acorn-65c102-coprocessor", "roms": [
            {"key":"client","filename":"acorn-tube-65c102_1_20.rom","size":4096,"display_name":"Acorn 65C102 Tube ROM 1.20"},
            {"key":"client","filename":"bare.rom","size":4096} ] }
        ] }
        """)
        let roms = schema.entry(forExtension: "acorn-65c102-coprocessor")?.roms ?? []
        XCTAssertEqual(roms.count, 2)
        XCTAssertEqual(roms.first?.label, "Acorn 65C102 Tube ROM 1.20")
        XCTAssertEqual(roms.last?.label, "bare")  // stem fallback
    }

    /// A bundled image shows the schema's friendly name, falling back to the stem.
    func testBundledImageDisplayName() {
        let named = BundledImage(image: "l3fs.dat", sizeBytes: 1, extensionName: nil,
                                 schemaDisplayName: "Acorn Level 3 File Server 1.26")
        let bare = BundledImage(image: "scratch.dat", sizeBytes: 1, extensionName: nil)
        XCTAssertEqual(named.displayName, "Acorn Level 3 File Server 1.26")
        XCTAssertEqual(bare.displayName, "scratch")
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
}
