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
import AppKit
@testable import Beebium

/// Issue #234 and client specification 7.4: a client's keyboard view shows the
/// active mapping's labelled bindings, built-in mappings included. This pins the
/// reference-table model that feeds the sidebar
/// (`KeyboardMappingManager.mappingReferenceEntries`) so it can never go empty
/// for a built-in again: with the general Logical mapping active it lists exactly
/// the five BBC keys that have no host namesake -- f0, Break, Copy, Shift Lock and
/// Delete -- each with its host key; a game mapping still lists its in-game
/// bindings.
///
/// The host-key codes asserted are macOS virtual key codes from the shared
/// macos-keycodes.json table (F10 = 109, F11 = 103, F13 = 105, F12 = 111,
/// Backspace = 51); the built-ins key on the neutral W3C `code` and the model
/// translates to the native code, so a row dropping out (a code missing from the
/// table) would fail here rather than silently vanish from the sidebar.
@MainActor
final class MappingReferenceTableTests: XCTestCase {

    private var manager: KeyboardMappingManager!

    override func setUp() async throws {
        try await super.setUp()
        await MainActor.run { manager = KeyboardMappingManager() }
    }

    override func tearDown() async throws {
        await MainActor.run { manager = nil }
        try await super.tearDown()
    }

    /// The built-in the manager loaded from the app bundle for this id (the very
    /// mappings the sidebar offers), failing the test if it is absent.
    private func builtIn(id: String) throws -> KeyboardMapping {
        let uuid = try XCTUnwrap(UUID(uuidString: id))
        return try XCTUnwrap(manager.mappings.first { $0.id == uuid },
                             "built-in \(id) is missing from the bundle")
    }

    /// The general Logical mapping must name exactly the five namesake-less keys,
    /// with the right host key and (for Break alone) the Option modifier.
    private func assertNamesTheFiveSpecialKeys(
        _ mapping: KeyboardMapping, file: StaticString = #filePath, line: UInt = #line
    ) {
        manager.activeMapping = mapping
        let rows = manager.mappingReferenceEntries

        XCTAssertEqual(rows.count, 5,
                       "\(mapping.name) must list exactly the five special keys, no more",
                       file: file, line: line)
        XCTAssertEqual(Set(rows.map(\.action)),
                       ["f0", "Copy", "Break", "Shift Lock", "Delete"],
                       "\(mapping.name) names the five BBC keys with no host namesake",
                       file: file, line: line)

        var byAction: [String: KeyboardMappingManager.MappingReferenceEntry] = [:]
        for row in rows { byAction[row.action] = row }

        XCTAssertEqual(byAction["f0"]?.keyCode, 109, file: file, line: line)         // F10
        XCTAssertEqual(byAction["Copy"]?.keyCode, 103, file: file, line: line)       // F11
        XCTAssertEqual(byAction["Shift Lock"]?.keyCode, 105, file: file, line: line) // F13
        XCTAssertEqual(byAction["Delete"]?.keyCode, 51, file: file, line: line)      // Backspace
        XCTAssertEqual(byAction["Break"]?.keyCode, 111, file: file, line: line)      // F12

        XCTAssertEqual(byAction["Break"]?.modifiers, [.option],
                       "Break is reached with Option held", file: file, line: line)
        for action in ["f0", "Copy", "Shift Lock", "Delete"] {
            XCTAssertEqual(byAction[action]?.modifiers, [],
                           "\(action) takes no host modifier", file: file, line: line)
        }

        for row in rows {
            XCTAssertFalse(row.hostKeyLabel.isEmpty,
                           "\(row.action) must render a host key, never a blank",
                           file: file, line: line)
        }
    }

    func testLogicalNamesExactlyTheFiveSpecialKeys() throws {
        try assertNamesTheFiveSpecialKeys(
            builtIn(id: "00000000-0000-0000-0000-000000000001"))
    }

    func testGameMappingStillListsItsBindings() throws {
        // Thrust's in-game labels are unaffected by the #234 built-in change.
        manager.activeMapping = try builtIn(id: "00000000-0000-0000-0000-000000000002")
        let actions = manager.mappingReferenceEntries.map(\.action)
        XCTAssertTrue(actions.contains("Fire"), "Thrust still lists Fire")
        XCTAssertTrue(actions.contains("Thrust"), "Thrust still lists Thrust")
        XCTAssertGreaterThanOrEqual(actions.count, 7,
                                    "Thrust still lists its several labelled keys")
    }
}
