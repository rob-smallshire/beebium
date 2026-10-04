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

@MainActor
final class MachineNameSequenceTests: XCTestCase {
    func testOrdinalsCountUpPerPreset() {
        let names = MachineNameSequence()

        XCTAssertEqual(names.nextOrdinal(forPreset: "BBC Model B"), 1)
        XCTAssertEqual(names.nextOrdinal(forPreset: "BBC Model B"), 2)
    }

    func testEachPresetCountsSeparately() {
        // Two machines from different presets are both the first of their
        // kind; sharing one counter would make the numbers say otherwise.
        let names = MachineNameSequence()

        XCTAssertEqual(names.nextOrdinal(forPreset: "BBC Model B"), 1)
        XCTAssertEqual(names.nextOrdinal(forPreset: "BBC Model B (Disc)"), 1)
        XCTAssertEqual(names.nextOrdinal(forPreset: "BBC Model B"), 2)
        XCTAssertEqual(names.nextOrdinal(forPreset: "BBC Model B (Disc)"), 2)
    }

    func testNumbersAreNeverReused() {
        // Nothing hands a number back: a number that has been on screen should
        // not reappear on a different machine later in the session.
        let names = MachineNameSequence()
        var seen: Set<Int> = []

        for _ in 0..<50 {
            let ordinal = names.nextOrdinal(forPreset: "BBC Model B")
            XCTAssertFalse(seen.contains(ordinal), "reissued \(ordinal)")
            seen.insert(ordinal)
        }
        XCTAssertEqual(seen.count, 50)
    }

    func testANewSessionStartsAgainAtOne() {
        // Counters are per run of the app and not persisted, so a fresh
        // sequence -- as a relaunch creates -- begins at #1.
        XCTAssertEqual(MachineNameSequence().nextOrdinal(forPreset: "BBC Model B"), 1)
        XCTAssertEqual(MachineNameSequence().nextOrdinal(forPreset: "BBC Model B"), 1)
    }
}

final class MachineLaunchNameTests: XCTestCase {
    func testPlainDisplayNameGainsOrdinalPlaceholder() {
        // No preset machine_name: the display name is the template, and the
        // ordinal placeholder is appended. Its value travels as --machine-ordinal
        // and the server renders it ("#2"), trimming any trailing space.
        XCTAssertEqual(
            MachineLaunchName.template(presetMachineName: nil,
                                       presetDisplayName: "BBC Model B"),
            "BBC Model B {machine-ordinal}")
    }

    func testEmptyMachineNameFallsBackToDisplayName() {
        // An empty machine_name is treated as absent.
        XCTAssertEqual(
            MachineLaunchName.template(presetMachineName: "",
                                       presetDisplayName: "BBC Model B"),
            "BBC Model B {machine-ordinal}")
    }

    func testPresetMachineNameTemplateIsUsedVerbatim() {
        // A preset's own template carries placeholders the server will render;
        // the app passes it through unchanged but for the appended ordinal.
        XCTAssertEqual(
            MachineLaunchName.template(
                presetMachineName: "Station {econet-station} (AUN, Model B)",
                presetDisplayName: "Station 80 (AUN, Model B)"),
            "Station {econet-station} (AUN, Model B) {machine-ordinal}")
    }

    func testBracesInADisplayNameAreEscaped() {
        // A literal name containing braces must not be read as a placeholder,
        // so the braces are doubled. The ordinal placeholder is left intact.
        XCTAssertEqual(
            MachineLaunchName.template(presetMachineName: nil,
                                       presetDisplayName: "Model {B}"),
            "Model {{B}} {machine-ordinal}")
    }

    func testEscapingBracesDoublesBoth() {
        XCTAssertEqual(MachineLaunchName.escapingBraces("a{b}c"), "a{{b}}c")
        XCTAssertEqual(MachineLaunchName.escapingBraces("no braces"), "no braces")
    }
}
