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

/// Tests for the pure rule that makes a saved blank disc carry its template's
/// own file extension, which the server needs to detect the format.
final class BlankDiscNamingTests: XCTestCase {
    private func corrected(_ chosen: String, template: String) -> String {
        BlankDiscNaming.url(
            forChosen: URL(fileURLWithPath: chosen),
            templateFilename: template).path
    }

    func testMatchingExtensionIsLeftAlone() {
        XCTAssertEqual(
            corrected("/discs/my game.dsd", template: "Acorn DFS 80T DS 400K.dsd"),
            "/discs/my game.dsd")
    }

    func testMatchIsCaseInsensitive() {
        XCTAssertEqual(
            corrected("/discs/my game.DSD", template: "Acorn DFS 80T DS 400K.dsd"),
            "/discs/my game.DSD")
    }

    func testMissingExtensionGetsTheTemplateExtension() {
        // The reported case: the user typed a bare name.
        XCTAssertEqual(
            corrected("/discs/blank-Acorn-DFS-80T-DS-400K", template: "Acorn DFS 80T DS 400K.dsd"),
            "/discs/blank-Acorn-DFS-80T-DS-400K.dsd")
    }

    func testDifferentExtensionGetsTheTemplateExtensionAppended() {
        // A .ssd name for a .dsd template must not reach the server as .ssd;
        // the template extension is appended so the format still reads right.
        XCTAssertEqual(
            corrected("/discs/blank.ssd", template: "Acorn DFS 80T DS 400K.dsd"),
            "/discs/blank.ssd.dsd")
    }

    func testTemplateWithoutExtensionLeavesTheChoiceUntouched() {
        // Defensive: a template file name with no extension cannot dictate one.
        XCTAssertEqual(
            corrected("/discs/blank", template: "weird-template-name"),
            "/discs/blank")
    }
}
