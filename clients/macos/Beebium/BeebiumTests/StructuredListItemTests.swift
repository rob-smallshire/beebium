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

/// The pure item_format rules for a structured `is_list` parameter: template
/// tokenisation, rendering an object to one line, parsing a legacy string back
/// to fields, and typing the result. Pinned on the two real shapes the server
/// emits (agreed with core-dev): the AUN map's peers and its subnet rules.
final class StructuredListItemTests: XCTestCase {

    // The AUN transport exactly as the catalogue reports it, with the two
    // structured-list parameters.
    private func aunManifest() throws -> ExtensionManifest {
        let schema = try JSONDecoder().decode(ExtensionsSchemaSection.self, from: Data("""
        { "type": "extensions", "attachment_points": [], "catalogue": [
          { "name": "aun", "display_name": "AUN", "kind": "econet-transport",
            "parameters": [
              {"key":"map","type":"string","is_list":true,"label":"Peers","position":0,
               "description":"Stations reachable over UDP.",
               "item_format":"{net}.{station}@{host}@{port}",
               "fields":[
                 {"key":"net","type":"integer","label":"Net","required":false,"default":"0","min":0,"max":255},
                 {"key":"station","type":"integer","label":"Station","required":true,"min":1,"max":254},
                 {"key":"host","type":"string","label":"Host","required":true},
                 {"key":"port","type":"integer","label":"Port","required":true,"min":1,"max":65535}]},
              {"key":"subnet","type":"string","is_list":true,"label":"Subnet rules","position":1,
               "item_format":"{net}@{address}",
               "fields":[
                 {"key":"net","type":"integer","label":"Net","required":true,"min":1,"max":255},
                 {"key":"address","type":"string","label":"Address","required":true}]} ] }
        ] }
        """.utf8))
        return schema.manifest(forExtension: "aun")
    }

    private func mapParameter() throws -> ExtensionParameter {
        try XCTUnwrap(try aunManifest().parameters.first { $0.key == "map" })
    }

    private func subnetParameter() throws -> ExtensionParameter {
        try XCTUnwrap(try aunManifest().parameters.first { $0.key == "subnet" })
    }

    // MARK: - Decoding fields

    func testFieldsAndItemFormatDecode() throws {
        let map = try mapParameter()
        XCTAssertTrue(map.isStructuredList)
        XCTAssertEqual(map.itemFormat, "{net}.{station}@{host}@{port}")
        XCTAssertEqual(map.fields.map(\.key), ["net", "station", "host", "port"])
        let net = try XCTUnwrap(map.fields.first { $0.key == "net" })
        XCTAssertEqual(net.type, "integer")
        XCTAssertFalse(net.required)
        XCTAssertEqual(net.defaultValue, "0")
        XCTAssertEqual(net.minimum, 0)
        XCTAssertEqual(net.maximum, 255)
        XCTAssertTrue(try XCTUnwrap(map.fields.first { $0.key == "station" }).required)
        // A plain scalar parameter is not a structured list.
        XCTAssertFalse(ExtensionManifest(name: "x", parameters: []).parameters.contains { $0.isStructuredList })
    }

    // MARK: - Template

    func testTemplateTokenises() throws {
        let template = ItemFormatTemplate("{net}.{station}@{host}@{port}")
        XCTAssertEqual(template.fieldKeys, ["net", "station", "host", "port"])
        XCTAssertEqual(template.prefix, "")
        XCTAssertEqual(template.suffix, "")
        XCTAssertEqual(template.separators, [".", "@", "@"])
    }

    // MARK: - Render (object -> line)

    func testRenderPeerWithNet() throws {
        let fields = try mapParameter().fields
        let line = StructuredListItem.render(
            object: ["net": .int(1), "station": .int(254),
                     "host": .string("10.0.0.5"), "port": .int(32768)],
            template: ItemFormatTemplate("{net}.{station}@{host}@{port}"), fields: fields)
        XCTAssertEqual(line, "1.254@10.0.0.5@32768")
    }

    func testRenderPeerDropsAbsentLeadingNet() throws {
        let fields = try mapParameter().fields
        // net absent (not required): the net and its "." separator drop.
        let line = StructuredListItem.render(
            object: ["station": .int(254), "host": .string("10.0.0.5"), "port": .int(32768)],
            template: ItemFormatTemplate("{net}.{station}@{host}@{port}"), fields: fields)
        XCTAssertEqual(line, "254@10.0.0.5@32768")
    }

    // MARK: - Parse (legacy string -> fields)

    func testParseBareStationLeadingNetAbsent() throws {
        let fields = try mapParameter().fields
        let strings = StructuredListItem.parse(
            string: "254@10.0.0.5@32768",
            template: ItemFormatTemplate("{net}.{station}@{host}@{port}"), fields: fields)
        XCTAssertNil(strings["net"])
        XCTAssertEqual(strings["station"], "254")
        XCTAssertEqual(strings["host"], "10.0.0.5")
        XCTAssertEqual(strings["port"], "32768")
    }

    func testParseWithNetPresent() throws {
        let fields = try mapParameter().fields
        let strings = StructuredListItem.parse(
            string: "1.254@10.0.0.5@32768",
            template: ItemFormatTemplate("{net}.{station}@{host}@{port}"), fields: fields)
        XCTAssertEqual(strings["net"], "1")
        XCTAssertEqual(strings["station"], "254")
        XCTAssertEqual(strings["host"], "10.0.0.5")
        XCTAssertEqual(strings["port"], "32768")
    }

    func testObjectFromStringTypesFields() throws {
        let fields = try mapParameter().fields
        let object = StructuredListItem.object(
            fromString: "1.254@10.0.0.5@32768",
            template: ItemFormatTemplate("{net}.{station}@{host}@{port}"), fields: fields)
        XCTAssertEqual(object["net"], .int(1))
        XCTAssertEqual(object["station"], .int(254))
        XCTAssertEqual(object["host"], .string("10.0.0.5"))
        XCTAssertEqual(object["port"], .int(32768))
    }

    func testRoundTripBareStation() throws {
        let fields = try mapParameter().fields
        let template = ItemFormatTemplate("{net}.{station}@{host}@{port}")
        let object = StructuredListItem.object(fromString: "254@10.0.0.5@32768",
                                               template: template, fields: fields)
        XCTAssertNil(object["net"])   // absent, not written back as 0
        XCTAssertEqual(StructuredListItem.render(object: object, template: template, fields: fields),
                       "254@10.0.0.5@32768")
    }

    // MARK: - Object from field strings (the commit form)

    func testObjectOmitsEmptyNotRequiredField() throws {
        let fields = try mapParameter().fields
        let object = StructuredListItem.object(
            fromFieldStrings: ["net": "", "station": "254", "host": "10.0.0.5", "port": "32768"],
            fields: fields)
        XCTAssertNil(object["net"])
        XCTAssertEqual(object["station"], .int(254))
    }

    func testObjectKeepsUnparseableIntegerVerbatim() throws {
        let fields = try mapParameter().fields
        // A required field the user left non-numeric is kept as a string so it is
        // not silently lost; resolve-preset flags it at the field path.
        let object = StructuredListItem.object(
            fromFieldStrings: ["station": "not-a-number", "host": "h", "port": "1"],
            fields: fields)
        XCTAssertEqual(object["station"], .string("not-a-number"))
    }

    // MARK: - Subnet (required leading field, no drop)

    func testSubnetRenderAndParse() throws {
        let fields = try subnetParameter().fields
        let template = ItemFormatTemplate("{net}@{address}")
        XCTAssertFalse(StructuredListItem.isLeadingOptional(template: template, fields: fields))
        XCTAssertEqual(StructuredListItem.render(
            object: ["net": .int(1), "address": .string("1.0.0.0/24")],
            template: template, fields: fields), "1@1.0.0.0/24")
        let object = StructuredListItem.object(fromString: "1@1.0.0.0/24",
                                               template: template, fields: fields)
        XCTAssertEqual(object["net"], .int(1))
        XCTAssertEqual(object["address"], .string("1.0.0.0/24"))
    }
}
