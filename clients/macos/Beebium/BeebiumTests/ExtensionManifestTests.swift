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

    /// The bundled-kind nouns as the server reports them, for the file-affordance
    /// wording tests (the client no longer carries this table).
    private let bundledKindNames = ["rom": "ROM", "hard-disc": "hard disc"]

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

    /// An attachment point carries its description, and a point without one
    /// still decodes (nil description).
    func testAttachmentPointDescription() throws {
        let schema = try schema(from: """
        { "type": "extensions", "attachment_points": [
          { "id": "user-port", "display_name": "User Port",
            "description": "A parallel connector for add-ons.",
            "provided_by": null, "extensions": ["acorn-rtc"] },
          { "id": "tube", "provided_by": null, "extensions": ["acorn-65c02-coprocessor"] }
        ] }
        """)
        let points = Dictionary(uniqueKeysWithValues: schema.attachmentPoints.map { ($0.id, $0) })
        XCTAssertEqual(points["user-port"]?.description, "A parallel connector for add-ons.")
        XCTAssertNil(points["tube"]?.description)
    }

    private func parameter(_ json: String) throws -> ExtensionParameter {
        try JSONDecoder().decode(ExtensionParameter.self, from: Data(json.utf8))
    }

    /// A parameter whose bundled default names a ROM shows that ROM's friendly
    /// name as the default; a parameter with no default has none.
    func testDefaultImageLabel() throws {
        let roms = [
            BundledImage(image: "acorn-tube-65c102_1_20.rom", sizeBytes: 4096,
                         extensionName: "acorn-65c102-coprocessor",
                         schemaDisplayName: "Acorn 65C102 Tube ROM 1.20"),
        ]
        let romParam = try parameter(
            #"{"key":"rom","type":"filepath","default":"acorn-tube-65c102_1_20.rom","bundled":"rom"}"#)
        XCTAssertEqual(
            ExtensionParameterForm.defaultImageLabel(forParameter: romParam, masters: roms),
            "Acorn 65C102 Tube ROM 1.20")
        XCTAssertEqual(ExtensionParameterForm.bundledSectionTitle(romParam, kinds: bundledKindNames),
                       "Bundled ROMs")

        // A disc image parameter defaults to empty: no bundled default.
        let imageParam = try parameter(
            #"{"key":"image","type":"filepath","default":"","bundled":"hard-disc"}"#)
        XCTAssertNil(ExtensionParameterForm.defaultImageLabel(forParameter: imageParam, masters: []))
        XCTAssertEqual(ExtensionParameterForm.bundledSectionTitle(imageParam, kinds: bundledKindNames),
                       "Bundled hard discs")

        // A default not among the masters falls back to the raw default name.
        let unknownDefault = try parameter(
            #"{"key":"rom","type":"filepath","default":"mystery.rom","bundled":"rom"}"#)
        XCTAssertEqual(
            ExtensionParameterForm.defaultImageLabel(forParameter: unknownDefault, masters: roms),
            "mystery.rom")
    }

    func testFileNounComesFromParameter() throws {
        // A serial-device filepath (Piconet's device_path) says "device", not "image".
        let device = try parameter(
            #"{"key":"device_path","type":"filepath","choices_from":"serial-ports"}"#)
        XCTAssertEqual(ExtensionParameterForm.fileNoun(device, kinds: bundledKindNames), "device")
        // A bundled image parameter says its kind, in the server's words.
        let rom = try parameter(#"{"key":"rom","type":"filepath","bundled":"rom"}"#)
        XCTAssertEqual(ExtensionParameterForm.fileNoun(rom, kinds: bundledKindNames), "ROM")
        let disc = try parameter(#"{"key":"image","type":"filepath","bundled":"hard-disc"}"#)
        XCTAssertEqual(ExtensionParameterForm.fileNoun(disc, kinds: bundledKindNames), "hard disc")
        // A plain filepath says "file".
        let plain = try parameter(#"{"key":"path","type":"filepath"}"#)
        XCTAssertEqual(ExtensionParameterForm.fileNoun(plain, kinds: bundledKindNames), "file")
        // A dirpath says "directory", so the labels read "No directory",
        // "Choose Directory...".
        let directory = try parameter(#"{"key":"directory","type":"dirpath"}"#)
        XCTAssertEqual(ExtensionParameterForm.fileNoun(directory, kinds: bundledKindNames), "directory")
    }

    /// Both a plain filepath and a plain dirpath map to the one filepath control.
    func testFilepathAndDirpathShareOneControl() throws {
        let file = try parameter(#"{"key":"path","type":"filepath"}"#)
        let directory = try parameter(#"{"key":"directory","type":"dirpath"}"#)
        XCTAssertEqual(ExtensionParameterForm.kind(of: file), .filepath)
        XCTAssertEqual(ExtensionParameterForm.kind(of: directory), .filepath)
    }

    /// The chooser is a plain "Choose..." button exactly when its menu would hold
    /// the one action: an empty value with no bundled masters, no default to
    /// return to and nothing to clear. Two or more actions keep the menu.
    func testFilepathChooserButtonVersusMenu() {
        // Empty plain filepath/dirpath: Choose alone -> a button.
        XCTAssertEqual(
            ExtensionParameterForm.filepathChooserActionCount(
                masterCount: 0, showsUseDefault: false, showsClear: false),
            1)
        // A set value with no default offers Choose + Clear -> a menu.
        XCTAssertEqual(
            ExtensionParameterForm.filepathChooserActionCount(
                masterCount: 0, showsUseDefault: false, showsClear: true),
            2)
        // A bundled parameter offers its masters plus Choose -> a menu.
        XCTAssertEqual(
            ExtensionParameterForm.filepathChooserActionCount(
                masterCount: 3, showsUseDefault: false, showsClear: false),
            4)
        // A chosen value against a bundled default offers Use default + Choose.
        XCTAssertEqual(
            ExtensionParameterForm.filepathChooserActionCount(
                masterCount: 0, showsUseDefault: true, showsClear: false),
            2)
    }

    func testBundledImageKindNounsComeFromSchema() throws {
        let withKinds = try schema(from: """
        { "type": "extensions", "attachment_points": [],
          "bundled_image_kinds": [
            {"kind":"hard-disc","display_name":"hard disc"},
            {"kind":"rom","display_name":"ROM"}
          ] }
        """)
        XCTAssertEqual(withKinds.bundledKindNames,
                       ["hard-disc": "hard disc", "rom": "ROM"])
        // An older server without the key yields an empty map, and a parameter
        // then falls back to the generic "image".
        let bare = try schema(from: #"{ "type": "extensions", "attachment_points": [] }"#)
        XCTAssertTrue(bare.bundledKindNames.isEmpty)
        let disc = try parameter(#"{"key":"image","type":"filepath","bundled":"hard-disc"}"#)
        XCTAssertEqual(ExtensionParameterForm.fileNoun(disc, kinds: bare.bundledKindNames), "image")
    }

    func testDecodesHostResolvedDefaultKeys() throws {
        // A host-resolved default (the AUN map file): "default" stays empty so an
        // exact-record preset writes nothing, while effective_default carries the
        // value in force and default_from names the resolver.
        let mapFile = try parameter("""
        {"key":"map-file","type":"string","default":"",\
        "default_from":"aun-map-file",\
        "effective_default":"/Users/x/Library/Application Support/Beebium/aun-map.json"}
        """)
        XCTAssertEqual(mapFile.defaultValue, "")
        XCTAssertEqual(mapFile.defaultFrom, "aun-map-file")
        XCTAssertEqual(mapFile.defaultInForce,
                       "/Users/x/Library/Application Support/Beebium/aun-map.json")

        // A plain parameter: effective_default equals default, so defaultInForce
        // is that value and it shows as a real value in the field.
        let port = try parameter(
            #"{"key":"port","type":"integer","default":"32768","effective_default":"32768"}"#)
        XCTAssertEqual(port.defaultFrom, "")
        XCTAssertEqual(port.defaultInForce, "32768")

        // An older server reports neither key: defaultInForce falls back to default.
        let legacy = try parameter(#"{"key":"net","type":"integer","default":"0"}"#)
        XCTAssertEqual(legacy.effectiveDefault, "")
        XCTAssertEqual(legacy.defaultInForce, "0")
    }

    func testMapFileChoiceFromRecordedValue() {
        // Nothing recorded -> the shared per-user default.
        XCTAssertEqual(ExtensionParameterForm.mapFileChoice(recorded: nil), .sharedDefault)
        XCTAssertEqual(ExtensionParameterForm.mapFileChoice(recorded: ""), .sharedDefault)
        // "none" -> no map file.
        XCTAssertEqual(ExtensionParameterForm.mapFileChoice(recorded: "none"), .none)
        // A path -> a file only this machine uses.
        XCTAssertEqual(ExtensionParameterForm.mapFileChoice(recorded: "/tmp/my-map.json"),
                       .custom("/tmp/my-map.json"))
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
