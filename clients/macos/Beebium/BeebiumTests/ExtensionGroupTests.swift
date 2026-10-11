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

/// Item B of the New Machine Configurator's acorn-rtc work: a manifest's groups
/// of mutually-exclusive parameters, choice descriptions, and the routing of the
/// plugin's own resolve-preset diagnostics. The schema keys are agreed with
/// core-dev; these pin the client's reading and projection of them.
final class ExtensionGroupTests: XCTestCase {

    private func schema(from json: String) throws -> ExtensionsSchemaSection {
        try JSONDecoder().decode(ExtensionsSchemaSection.self, from: Data(json.utf8))
    }

    /// acorn-rtc as the catalogue reports it: a layout choice whose options carry
    /// descriptions, and one group ("Clock start") of two mutually-exclusive
    /// members with a "Host time" none option.
    private func acornRtcSchema() throws -> ExtensionsSchemaSection {
        try schema(from: """
        { "type": "extensions", "attachment_points": [], "catalogue": [
          { "name": "acorn-rtc", "display_name": "Acorn Real-Time Clock",
            "kind": "peripheral",
            "parameters": [
              {"key":"layout","type":"string","position":0,"default":"7bit-year-in-r7",
               "choices":[
                 {"value":"7bit-year-in-r7","label":"Level 3 File Server 1.26",
                  "description":"Years 1981 to 2099."},
                 {"value":"acorn-original","label":"Acorn original (4-bit year in R1)",
                  "description":"Years 1981 to 1996, as the original Acorn file servers expect. Not Y2K compliant."}]},
              {"key":"time","type":"string","position":1,"excludes":["offset"]},
              {"key":"offset","type":"string","position":2,"excludes":["time"]} ],
            "groups": [
              {"key":"start","label":"Clock start",
               "description":"Where the clock starts when the machine is switched on.",
               "parameters":["time","offset"],
               "none":{"label":"Host time","description":"The clock follows this computer's clock."}} ] }
        ] }
        """)
    }

    // MARK: - Groups decode

    /// The group decodes with its label, description, members (in order) and its
    /// none option, and the manifest carries it.
    func testGroupDecodes() throws {
        let manifest = try acornRtcSchema().manifest(forExtension: "acorn-rtc")
        XCTAssertEqual(manifest.groups.count, 1)
        let group = try XCTUnwrap(manifest.groups.first)
        XCTAssertEqual(group.key, "start")
        XCTAssertEqual(group.label, "Clock start")
        XCTAssertEqual(group.description, "Where the clock starts when the machine is switched on.")
        XCTAssertEqual(group.parameters, ["time", "offset"])
        XCTAssertEqual(group.none?.label, "Host time")
        XCTAssertEqual(group.none?.description, "The clock follows this computer's clock.")
    }

    /// A parameter maps back to the group it belongs to; an ungrouped one does not.
    func testGroupForParameter() throws {
        let manifest = try acornRtcSchema().manifest(forExtension: "acorn-rtc")
        XCTAssertEqual(manifest.group(forParameter: "time")?.key, "start")
        XCTAssertEqual(manifest.group(forParameter: "offset")?.key, "start")
        XCTAssertNil(manifest.group(forParameter: "layout"))
    }

    /// A group with no "nothing given" state decodes with a nil none option, and
    /// an absent groups array is simply empty (older servers).
    func testGroupWithoutNoneAndAbsentGroups() throws {
        let withoutNone = try schema(from: """
        { "type": "extensions", "attachment_points": [], "catalogue": [
          { "name": "x", "parameters": [
            {"key":"a","type":"string","position":0},
            {"key":"b","type":"string","position":1} ],
            "groups": [ {"key":"g","label":"G","parameters":["a","b"]} ] }
        ] }
        """)
        XCTAssertNil(withoutNone.manifest(forExtension: "x").groups.first?.none)

        let noGroups = try schema(from: """
        { "type": "extensions", "attachment_points": [], "catalogue": [
          { "name": "y", "parameters": [{"key":"a","type":"string"}] } ]
        }
        """)
        XCTAssertTrue(noGroups.manifest(forExtension: "y").groups.isEmpty)
    }

    // MARK: - Choice descriptions

    /// A choice carries its description, and an older choice without one decodes
    /// with an empty description.
    func testChoiceDescriptions() throws {
        let manifest = try acornRtcSchema().manifest(forExtension: "acorn-rtc")
        let layout = try XCTUnwrap(manifest.parameters.first { $0.key == "layout" })
        let byValue = Dictionary(uniqueKeysWithValues: layout.choices.map { ($0.value, $0) })
        XCTAssertEqual(byValue["7bit-year-in-r7"]?.description, "Years 1981 to 2099.")
        XCTAssertTrue(byValue["acorn-original"]?.description.contains("Not Y2K compliant") ?? false)

        let old = try JSONDecoder().decode(ExtensionParameter.Choice.self,
                                           from: Data(#"{"value":"v","label":"L"}"#.utf8))
        XCTAssertEqual(old.description, "")
    }

    // MARK: - Group selection logic (pure)

    private func group() throws -> ExtensionGroup {
        try XCTUnwrap(try acornRtcSchema().manifest(forExtension: "acorn-rtc").groups.first)
    }

    /// The selected member is the one with a value; none when the draft sets
    /// neither (the Host time state).
    func testSelectedMemberKey() throws {
        let g = try group()
        XCTAssertNil(ExtensionParameterForm.selectedMemberKey(group: g, config: [:]))
        XCTAssertEqual(ExtensionParameterForm.selectedMemberKey(
            group: g, config: ["time": .string("1991-01-01")]), "time")
        XCTAssertEqual(ExtensionParameterForm.selectedMemberKey(
            group: g, config: ["offset": .string("-1h")]), "offset")
        // An empty string is not a value, so it still reads as "none".
        XCTAssertNil(ExtensionParameterForm.selectedMemberKey(
            group: g, config: ["time": .string("")]))
    }

    /// The initial selection prefers a set member, else the none option when the
    /// group has one, else the first member.
    func testInitialSelection() throws {
        let g = try group()
        XCTAssertEqual(ExtensionParameterForm.initialSelection(group: g, config: [:]), "")
        XCTAssertEqual(ExtensionParameterForm.initialSelection(
            group: g, config: ["offset": .string("-1h")]), "offset")

        var noneless = g
        noneless = ExtensionGroup(key: g.key, label: g.label, description: g.description,
                                  parameters: g.parameters, none: nil)
        XCTAssertEqual(ExtensionParameterForm.initialSelection(group: noneless, config: [:]), "time")
    }

    /// Picking an option clears every other member that has a value; picking the
    /// chosen member clears nothing; picking none clears all.
    func testKeysToClear() throws {
        let g = try group()
        let timeSet: [String: JSONValue] = ["time": .string("1991-01-01")]
        XCTAssertEqual(ExtensionParameterForm.keysToClear(group: g, picking: "offset", config: timeSet), ["time"])
        XCTAssertEqual(ExtensionParameterForm.keysToClear(group: g, picking: "time", config: timeSet), [])
        XCTAssertEqual(ExtensionParameterForm.keysToClear(group: g, picking: "", config: timeSet), ["time"])
        // Nothing set: nothing to clear whatever is picked (the exact-record
        // Host time state writes nothing).
        XCTAssertEqual(ExtensionParameterForm.keysToClear(group: g, picking: "time", config: [:]), [])
    }

    // MARK: - Plugin diagnostics routing (B.3)

    /// The client routes the plugin's resolve-preset diagnostics to the right
    /// place by their JSON pointer, and blocks Create by severity -- the same
    /// mechanism every preset diagnostic uses, so no acorn-rtc knowledge is
    /// needed in the client.
    ///
    /// The three acorn-rtc scenarios map onto this contract:
    ///  - 2001 start time on the Acorn original layout: an error at
    ///    /extensions/0/config/time -> shows on the time field and blocks Create.
    ///  - the default layout on Host time: no diagnostics -> clean, Create allowed.
    ///  - an offset the layout cannot hold: a diagnostic at
    ///    /extensions/0/config/offset -> shows on the offset field; a warning
    ///    there does not block, an error there does.
    func testPluginDiagnosticsRouteByPointerAndBlockBySeverity() {
        typealias D = PresetManager.PresetDiagnostic
        let timeError = D(severity: "error", path: "/extensions/0/config/time",
                          message: "year 2001 is outside 1981-1996", location: "Acorn RTC")
        let offsetWarning = D(severity: "warning", path: "/extensions/0/config/offset",
                              message: "offset lands outside the range today", location: "Acorn RTC")
        let instanceError = D(severity: "error", path: "/extensions/0",
                              message: "host time cannot be held by this layout", location: "Acorn RTC")

        let diags = [timeError, offsetWarning]

        // Routed to the field named by the pointer.
        XCTAssertEqual(diags.forConfigKey("time"), [timeError])
        XCTAssertEqual(diags.forConfigKey("offset"), [offsetWarning])
        // Both belong to instance 0.
        XCTAssertEqual(Set(diags.forExtension(index: 0).map(\.path)),
                       Set([timeError.path, offsetWarning.path]))

        // An error blocks Create; a lone warning does not.
        XCTAssertTrue([timeError, offsetWarning].contains(where: \.isError))
        XCTAssertFalse([offsetWarning].contains(where: \.isError))
        XCTAssertFalse([D]().contains(where: \.isError))   // host time, default layout: clean

        // An instance-level diagnostic (no /config) routes to the instance, not
        // to any field.
        XCTAssertNil(PresetDiagnosticLocation.configKey(path: instanceError.path))
        XCTAssertEqual(PresetDiagnosticLocation.extensionIndex(path: instanceError.path), 0)
        XCTAssertTrue([instanceError].forConfigKey("time").isEmpty)
        XCTAssertEqual([instanceError].forExtension(index: 0), [instanceError])
    }
}
