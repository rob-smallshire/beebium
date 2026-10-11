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

/// The adapter that lets the sidebar's EditableList stack render a structured
/// `is_list` parameter from the draft: the Beebium_EditableList it builds
/// outward, the new items an EditableListIntent produces inward, and the
/// routing of resolve-preset diagnostics to a row and its field. No view is
/// forked; these pin the pure data on both sides of the shared views.
final class StructuredListAdapterTests: XCTestCase {

    private func mapParameter() throws -> ExtensionParameter {
        let schema = try JSONDecoder().decode(ExtensionsSchemaSection.self, from: Data("""
        { "type": "extensions", "attachment_points": [], "catalogue": [
          { "name": "aun", "kind": "econet-transport", "parameters": [
            {"key":"map","type":"string","is_list":true,"label":"Peers",
             "description":"Stations reachable over UDP.",
             "item_format":"{net}.{station}@{host}@{port}",
             "fields":[
               {"key":"net","type":"integer","label":"Net","required":false,"default":"0","min":0,"max":255},
               {"key":"station","type":"integer","label":"Station","required":true,"min":1,"max":254},
               {"key":"host","type":"string","label":"Host","required":true},
               {"key":"port","type":"integer","label":"Port","required":true,"min":1,"max":65535}]} ] }
        ] }
        """.utf8))
        return try XCTUnwrap(schema.manifest(forExtension: "aun").parameters.first { $0.key == "map" })
    }

    private func peer(net: Int?, station: Int, host: String, port: Int) -> JSONValue {
        var object: [String: JSONValue] = ["station": .int(station), "host": .string(host), "port": .int(port)]
        if let net = net { object["net"] = .int(net) }
        return .object(object)
    }

    // MARK: - Outward: EditableList

    func testEditableListTitleItemsAndAddEditor() throws {
        let map = try mapParameter()
        let items = [peer(net: nil, station: 254, host: "10.0.0.5", port: 32768),
                     peer(net: 1, station: 2, host: "10.0.0.9", port: 10000)]
        let list = StructuredListParameterView.editableList(parameter: map, items: items, diagnostics: [])

        XCTAssertEqual(list.title, "Peers (2)")
        XCTAssertEqual(list.help, "Stations reachable over UDP.")
        XCTAssertTrue(list.canAdd)
        XCTAssertEqual(list.items.count, 2)
        XCTAssertEqual(list.items[0].primary, "254@10.0.0.5@32768")
        XCTAssertEqual(list.items[1].primary, "1.2@10.0.0.9@10000")
        XCTAssertTrue(list.items[0].editable)
        XCTAssertTrue(list.items[0].removable)
        XCTAssertEqual(list.items[0].id, "0")
        // The add editor is a group of one leaf per field, empty (defaults shown
        // as placeholders).
        XCTAssertEqual(list.addEditor.group.controls.map { $0.id }, ["net", "station", "host", "port"])
        let addNet = list.addEditor.group.controls[0]
        XCTAssertEqual(addNet.textInput.value, "")
        XCTAssertEqual(addNet.textInput.placeholder, "0")       // the net default
        XCTAssertEqual(addNet.textInput.label, "Net")
    }

    func testEmptyListTitleAndText() throws {
        let list = StructuredListParameterView.editableList(parameter: try mapParameter(),
                                                            items: [], diagnostics: [])
        XCTAssertEqual(list.title, "Peers")
        XCTAssertEqual(list.emptyText, "No peers")
        XCTAssertTrue(list.items.isEmpty)
    }

    func testEditEditorPrefilledFromItem() throws {
        let map = try mapParameter()
        let list = StructuredListParameterView.editableList(
            parameter: map, items: [peer(net: 1, station: 254, host: "10.0.0.5", port: 32768)],
            diagnostics: [])
        let controls = list.items[0].editor.group.controls
        let byID = Dictionary(uniqueKeysWithValues: controls.map { ($0.id, $0) })
        XCTAssertEqual(byID["net"]?.textInput.value, "1")
        XCTAssertEqual(byID["station"]?.textInput.value, "254")
        XCTAssertEqual(byID["host"]?.textInput.value, "10.0.0.5")
        XCTAssertEqual(byID["port"]?.textInput.value, "32768")
        XCTAssertEqual(byID["host"]?.textInput.help, "")
    }

    func testLegacyStringItemShownAndPrefilledParsed() throws {
        let map = try mapParameter()
        let list = StructuredListParameterView.editableList(
            parameter: map, items: [.string("254@10.0.0.5@32768")], diagnostics: [])
        // A legacy string is its own display line.
        XCTAssertEqual(list.items[0].primary, "254@10.0.0.5@32768")
        // Its editor is prefilled from the parse (net absent).
        let byID = Dictionary(uniqueKeysWithValues: list.items[0].editor.group.controls.map { ($0.id, $0) })
        XCTAssertEqual(byID["net"]?.textInput.value, "")
        XCTAssertEqual(byID["station"]?.textInput.value, "254")
    }

    // MARK: - Inward: apply intents

    private func commit(_ pairs: [String: String]) -> [EditorFieldCommit] {
        pairs.map { EditorFieldCommit(fieldID: $0.key, value: .string($0.value)) }
    }

    func testApplyAddOmitsEmptyNet() throws {
        let map = try mapParameter()
        let intent = EditableListIntent(kind: .add, itemID: "", actionID: "",
            commit: commit(["net": "", "station": "254", "host": "10.0.0.5", "port": "32768"]))
        let result = StructuredListParameterView.apply(intent, to: [], fields: map.fields)
        XCTAssertEqual(result.count, 1)
        XCTAssertEqual(result[0], peer(net: nil, station: 254, host: "10.0.0.5", port: 32768))
    }

    func testApplyAddWithNet() throws {
        let map = try mapParameter()
        let intent = EditableListIntent(kind: .add, itemID: "", actionID: "",
            commit: commit(["net": "5", "station": "2", "host": "h", "port": "1"]))
        let result = StructuredListParameterView.apply(intent, to: [], fields: map.fields)
        XCTAssertEqual(result[0], peer(net: 5, station: 2, host: "h", port: 1))
    }

    func testApplyEditReplacesRow() throws {
        let map = try mapParameter()
        let items = [peer(net: nil, station: 1, host: "a", port: 1),
                     peer(net: nil, station: 2, host: "b", port: 2)]
        let intent = EditableListIntent(kind: .edit, itemID: "1", actionID: "",
            commit: commit(["station": "9", "host": "z", "port": "9"]))
        let result = StructuredListParameterView.apply(intent, to: items, fields: map.fields)
        XCTAssertEqual(result[1], peer(net: nil, station: 9, host: "z", port: 9))
        XCTAssertEqual(result[0], items[0])
    }

    func testApplyRemoveDropsRow() throws {
        let map = try mapParameter()
        let items = [peer(net: nil, station: 1, host: "a", port: 1),
                     peer(net: nil, station: 2, host: "b", port: 2)]
        let intent = EditableListIntent(kind: .remove, itemID: "0", actionID: "", commit: [])
        let result = StructuredListParameterView.apply(intent, to: items, fields: map.fields)
        XCTAssertEqual(result, [items[1]])
    }

    func testApplyEditOutOfRangeIsNoOp() throws {
        let map = try mapParameter()
        let items = [peer(net: nil, station: 1, host: "a", port: 1)]
        let intent = EditableListIntent(kind: .edit, itemID: "9", actionID: "",
            commit: commit(["station": "9", "host": "z", "port": "9"]))
        XCTAssertEqual(StructuredListParameterView.apply(intent, to: items, fields: map.fields), items)
    }

    func testCommitStringsResolvesChoiceIndex() throws {
        // A field with choices commits an index, which maps back to the value.
        let field = try JSONDecoder().decode(ExtensionListField.self, from: Data("""
        {"key":"mode","type":"string","choices":[
          {"value":"a","label":"Alpha"},{"value":"b","label":"Beta"}]}
        """.utf8))
        let strings = StructuredListParameterView.commitStrings(
            [EditorFieldCommit(fieldID: "mode", value: .index(1))], fields: [field])
        XCTAssertEqual(strings["mode"], "b")
    }

    // MARK: - Diagnostics routing (item 2 contract)

    func testDiagnosticsRouteToRowAndField() throws {
        typealias D = PresetManager.PresetDiagnostic
        let map = try mapParameter()
        let items = [peer(net: nil, station: 1, host: "a", port: 1),
                     peer(net: nil, station: 2, host: "bad-host", port: 2)]
        let diagnostics = [
            D(severity: "error", path: "/extensions/0/config/map/1/host",
              message: "host must be an IPv4 address", location: "AUN"),
            D(severity: "warning", path: "/extensions/0/config/map/0",
              message: "peer unreachable", location: "AUN")]
        let list = StructuredListParameterView.editableList(parameter: map, items: items,
                                                            diagnostics: diagnostics)
        // Row 0: an item-level warning marks the row.
        XCTAssertEqual(list.items[0].note, "peer unreachable")
        XCTAssertEqual(list.items[0].state, .warn)
        // Row 1: a field-level error marks the row and the host field's note.
        XCTAssertEqual(list.items[1].note, "host must be an IPv4 address")
        let host = try XCTUnwrap(list.items[1].editor.group.controls.first { $0.id == "host" })
        XCTAssertEqual(host.textInput.note, "host must be an IPv4 address")
        // A field that is not flagged carries no note.
        let port = try XCTUnwrap(list.items[1].editor.group.controls.first { $0.id == "port" })
        XCTAssertEqual(port.textInput.note, "")
    }

    func testDiagnosticRoutingIsPrefixIndependent() {
        typealias D = PresetManager.PresetDiagnostic
        // The same helper routes the econet transport form, whose paths sit under
        // /econet/transport/parameters/<key> rather than /extensions/<i>/config.
        let extensionsPath = "/extensions/0/config/map/2/host"
        let econetPath = "/econet/transport/parameters/map/2/host"
        XCTAssertEqual(PresetDiagnosticLocation.listItem(path: extensionsPath, key: "map")?.index, 2)
        XCTAssertEqual(PresetDiagnosticLocation.listItem(path: extensionsPath, key: "map")?.field, "host")
        XCTAssertEqual(PresetDiagnosticLocation.listItem(path: econetPath, key: "map")?.index, 2)
        XCTAssertEqual(PresetDiagnosticLocation.listItem(path: econetPath, key: "map")?.field, "host")
        // A malformed whole-string item has no field.
        XCTAssertEqual(PresetDiagnosticLocation.listItem(path: "/extensions/0/config/map/3", key: "map")?.field, nil)
        XCTAssertEqual(PresetDiagnosticLocation.listItem(path: "/extensions/0/config/map/3", key: "map")?.index, 3)
        // Not an item of this key.
        XCTAssertNil(PresetDiagnosticLocation.listItem(path: "/extensions/0/config/baud", key: "map"))
        XCTAssertNil(PresetDiagnosticLocation.listItem(path: "/econet/station", key: "map"))
    }

    func testConfigKeyExcludesDeeperItemPaths() {
        // A parameter-level diagnostic resolves to its key; an item/field path
        // under it does not (it routes by listItem instead), so the parameter
        // label does not also show the row's problem.
        XCTAssertEqual(PresetDiagnosticLocation.configKey(path: "/extensions/0/config/baud"), "baud")
        XCTAssertNil(PresetDiagnosticLocation.configKey(path: "/extensions/0/config/map/0/host"))
        XCTAssertNil(PresetDiagnosticLocation.configKey(path: "/extensions/0/config/map/0"))
    }

    func testForListItemArrayHelpers() {
        typealias D = PresetManager.PresetDiagnostic
        let diagnostics = [
            D(severity: "error", path: "/extensions/0/config/map/1/host", message: "a", location: "AUN"),
            D(severity: "warning", path: "/extensions/0/config/map/0", message: "b", location: "AUN"),
            D(severity: "error", path: "/extensions/0/config/baud", message: "c", location: "AUN")]
        XCTAssertEqual(diagnostics.forListItem(key: "map", index: 1).map(\.message), ["a"])
        XCTAssertEqual(diagnostics.forListItemField(key: "map", index: 1, field: "host").map(\.message), ["a"])
        XCTAssertTrue(diagnostics.forListItemField(key: "map", index: 1, field: "port").isEmpty)
        XCTAssertTrue(diagnostics.isListItem(key: "map"))
        XCTAssertFalse(diagnostics.isListItem(key: "subnet"))
    }
}
