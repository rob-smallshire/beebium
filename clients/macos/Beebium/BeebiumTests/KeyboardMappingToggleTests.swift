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

/// The Edit > Switch to … Keyboard Mapping toggle (Cmd-K): what "previous" is and
/// when it is cleared. The toggle bounces to Logical from any other mapping and
/// back to the previously active mapping from Logical; its menu item is disabled
/// when there is no previous to return to (`toggleTargetMapping` is nil).
///
/// Issue #260: deleting the mapping recorded as "previous" must clear it, so the
/// item reverts to the disabled "Switch to Previous Keyboard Mapping" state
/// rather than pointing at a mapping that no longer exists.
@MainActor
final class KeyboardMappingToggleTests: XCTestCase {

    private var manager: KeyboardMappingManager!

    override func setUp() async throws {
        try await super.setUp()
        await MainActor.run { manager = KeyboardMappingManager() }
    }

    override func tearDown() async throws {
        await MainActor.run { manager = nil }
        try await super.tearDown()
    }

    /// The default Logical mapping the manager loads from the bundle.
    private func logical() throws -> KeyboardMapping {
        try XCTUnwrap(
            manager.mappings.first { $0.id == KeyboardMappingManager.defaultLogicalMappingID },
            "the default Logical mapping is missing from the bundle")
    }

    /// A fresh user mapping cloned from Logical and added to the manager.
    private func userMapping(_ name: String) throws -> KeyboardMapping {
        manager.cloneMapping(try logical(), newName: name)
    }

    /// On a fresh manager the active mapping is Logical with no previous, so the
    /// toggle has nowhere to go and the menu item is disabled.
    func testFreshManagerHasNoPreviousAndDisabledToggle() throws {
        XCTAssertEqual(manager.activeMapping?.id, try logical().id)
        XCTAssertNil(manager.previousMapping)
        XCTAssertNil(manager.toggleTargetMapping, "no previous yet -> item disabled")
    }

    /// Deleting the mapping recorded as "previous" clears it, disabling the item.
    func testDeletingThePreviousMappingClearsItAndDisablesToggle() throws {
        let logical = try logical()
        let userA = try userMapping("Alpha")

        manager.activeMapping = userA      // previous becomes Logical
        manager.activeMapping = logical    // previous becomes userA; now on Logical
        XCTAssertEqual(manager.previousMapping?.id, userA.id)
        XCTAssertEqual(manager.toggleTargetMapping?.id, userA.id, "enabled -> returns to userA")

        manager.deleteMapping(userA)

        XCTAssertNil(manager.previousMapping, "the deleted previous is cleared (#260)")
        XCTAssertNil(manager.toggleTargetMapping, "item reverts to disabled")
    }

    /// Deleting a mapping that is not the current "previous" leaves it intact.
    func testDeletingANonPreviousMappingLeavesPreviousIntact() throws {
        let logical = try logical()
        let userA = try userMapping("Alpha")
        let userB = try userMapping("Beta")

        manager.activeMapping = userA      // previous: Logical
        manager.activeMapping = userB      // previous: userA
        manager.activeMapping = logical    // previous: userB; now on Logical
        XCTAssertEqual(manager.previousMapping?.id, userB.id)

        manager.deleteMapping(userA)       // userA is not the previous

        XCTAssertEqual(manager.previousMapping?.id, userB.id, "an unrelated delete does not touch previous")
        XCTAssertEqual(manager.toggleTargetMapping?.id, userB.id)
    }

    /// Deleting the active mapping must not leave that deleted mapping behind as
    /// "previous": the auto-switch to another mapping would otherwise record it.
    func testDeletingActiveMappingDoesNotResurrectItAsPrevious() throws {
        let userA = try userMapping("Alpha")

        manager.activeMapping = userA      // active userA, previous Logical

        manager.deleteMapping(userA)       // auto-switches away from the deleted active

        XCTAssertNotEqual(manager.activeMapping?.id, userA.id, "active moved off the deleted mapping")
        XCTAssertNil(manager.previousMapping, "the deleted active is not recorded as previous")
        XCTAssertNil(manager.toggleTargetMapping)
    }
}
