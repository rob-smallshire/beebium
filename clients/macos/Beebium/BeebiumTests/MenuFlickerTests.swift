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
import Combine
import GRPC
@testable import Beebium

/// The two independent barriers that stop the #290 menu flicker (Edit and Window
/// menus rebuilding at the 500 ms heartbeat on macOS 26/27).
///
/// Barrier 1 (root cause): `SystemClient` publishes only on a real change, so a
/// heartbeat that repeats the current state fires no `objectWillChange` and no
/// window body re-evaluates. Barrier 2 (belt and braces): the command-dependent
/// focused values a machine window attaches are value types with a stable
/// identity, so two values built in successive body passes compare equal and
/// SwiftUI does not rebuild the open menus even if a body pass does happen.
@MainActor
final class MenuFlickerTests: XCTestCase {

    private func heartbeat(templateMediaWritten: Bool) -> Beebium_ServerStatusEvent {
        var event = Beebium_ServerStatusEvent()
        event.status = .serverStatusHeartbeat
        event.templateMediaWritten = templateMediaWritten
        return event
    }

    // MARK: - Barrier 1: publish only on change

    func testRepeatedHeartbeatsWithUnchangedStateDoNotPublish() {
        let client = SystemClient()
        // Establish the baseline so the first counted heartbeat is a repeat, not
        // itself a change away from the default.
        client.handleStatusEvent(heartbeat(templateMediaWritten: false))

        var changes = 0
        let token = client.objectWillChange.sink { _ in changes += 1 }
        defer { token.cancel() }

        for _ in 0..<20 {
            client.handleStatusEvent(heartbeat(templateMediaWritten: false))
        }

        XCTAssertEqual(changes, 0,
            "A heartbeat repeating the current state must not fire objectWillChange; "
            + "an unconditional @Published assignment every 500 ms is the #290 root cause.")
    }

    func testHeartbeatThatChangesStatePublishesExactlyOnce() {
        let client = SystemClient()
        client.handleStatusEvent(heartbeat(templateMediaWritten: false))

        var changes = 0
        let token = client.objectWillChange.sink { _ in changes += 1 }
        defer { token.cancel() }

        client.handleStatusEvent(heartbeat(templateMediaWritten: true))

        XCTAssertEqual(changes, 1,
            "A heartbeat that actually changes templateMediaWritten must still publish once.")
        XCTAssertTrue(client.templateMediaWritten)
    }

    // MARK: - Barrier 2: stable, Equatable focused values

    func testMachineSaveActionsEqualForSameMachineAndState() {
        let client = SystemClient()
        let a = MachineSaveActions(isSaved: false, isServerLocal: true,
                                   save: {}, saveAs: {},
                                   machineIdentity: ObjectIdentifier(client))
        let b = MachineSaveActions(isSaved: false, isServerLocal: true,
                                   save: {}, saveAs: {},
                                   machineIdentity: ObjectIdentifier(client))
        XCTAssertEqual(a, b,
            "Two values for the same machine and state must be equal so a body pass "
            + "does not rebuild the open menus (#290).")
    }

    func testMachineSaveActionsDifferWhenSavedStateChanges() {
        let client = SystemClient()
        let unsaved = MachineSaveActions(isSaved: false, isServerLocal: true,
                                         save: {}, saveAs: {},
                                         machineIdentity: ObjectIdentifier(client))
        let saved = MachineSaveActions(isSaved: true, isServerLocal: true,
                                       save: {}, saveAs: {},
                                       machineIdentity: ObjectIdentifier(client))
        XCTAssertNotEqual(unsaved, saved,
            "A real state change must change the value so Save/Save As update.")
    }

    func testMachineSaveActionsDifferAcrossMachines() {
        let one = SystemClient()
        let two = SystemClient()
        let a = MachineSaveActions(isSaved: false, isServerLocal: true,
                                   save: {}, saveAs: {},
                                   machineIdentity: ObjectIdentifier(one))
        let b = MachineSaveActions(isSaved: false, isServerLocal: true,
                                   save: {}, saveAs: {},
                                   machineIdentity: ObjectIdentifier(two))
        XCTAssertNotEqual(a, b,
            "Different machine windows must carry distinct values.")
    }

    func testRenameMachineActionEqualForSameMachine() {
        let client = SystemClient()
        let a = RenameMachineAction(machineIdentity: ObjectIdentifier(client), perform: {})
        let b = RenameMachineAction(machineIdentity: ObjectIdentifier(client), perform: {})
        XCTAssertEqual(a, b)
    }

    func testRenameMachineActionDiffersAcrossMachines() {
        let one = SystemClient()
        let two = SystemClient()
        XCTAssertNotEqual(
            RenameMachineAction(machineIdentity: ObjectIdentifier(one), perform: {}),
            RenameMachineAction(machineIdentity: ObjectIdentifier(two), perform: {}))
    }

    func testRenameMachineActionInvokesItsClosure() {
        let client = SystemClient()
        var called = false
        let action = RenameMachineAction(machineIdentity: ObjectIdentifier(client)) {
            called = true
        }
        action()
        XCTAssertTrue(called, "callAsFunction must run the wrapped rename closure.")
    }
}
