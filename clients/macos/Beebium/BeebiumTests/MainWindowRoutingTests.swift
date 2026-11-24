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

/// The routing decision behind #310 (a preset card's first click is lost) and
/// #309 (the first File > Connect fails). A main-group window must decide at
/// creation whether it is a machine window or the Welcome window, by consuming
/// any pending connection target then -- not in `.onAppear`, which let a
/// machine-bound window first render as a Welcome and lose the
/// `WelcomeWindowRegistry` race to the real Welcome.
@MainActor
final class MainWindowRoutingTests: XCTestCase {
    override func setUp() {
        super.setUp()
        // The pending target lives on a shared singleton; start every test from
        // a clean slate so one test's leftover target cannot leak into another.
        _ = ConnectWindowState.shared.consumePendingTarget()
    }

    override func tearDown() {
        _ = ConnectWindowState.shared.consumePendingTarget()
        super.tearDown()
    }

    /// A window created while a target is pending is a machine window, carrying
    /// the full launch tuple -- so its first render is `ContentView`, never the
    /// Welcome content that registers with `WelcomeWindowRegistry`.
    func testPendingTargetProducesMachineWindow() {
        let target = ConnectionTarget(host: "127.0.0.1", port: 48875)
        let state = ConnectWindowState.shared
        state.pendingTarget = target
        state.pendingNeedsRun = true
        state.pendingProvenanceUUID = "prov-123"
        state.pendingShowSidebar = false

        let role = MainWindowRole()

        XCTAssertTrue(role.kind.isMachine)
        XCTAssertEqual(
            role.kind,
            .machine(target: target, needsRun: true,
                     provenanceUUID: "prov-123", showSidebar: false))
    }

    /// With no target pending, a window is the Welcome window.
    func testNoPendingTargetProducesWelcomeWindow() {
        let role = MainWindowRole()
        XCTAssertEqual(role.kind, .welcome)
        XCTAssertFalse(role.kind.isMachine)
    }

    /// The pending target is consumed exactly once, at creation. The window it
    /// was set for claims it; a second window (or the same view re-initialised)
    /// is a plain Welcome. This is what stops a stray target leaking into a
    /// later window, and what guarantees only one window connects per launch.
    func testPendingTargetIsConsumedExactlyOnce() {
        let target = ConnectionTarget(host: "localhost", port: 50123)
        ConnectWindowState.shared.pendingTarget = target

        let first = MainWindowRole()
        let second = MainWindowRole()

        XCTAssertTrue(first.kind.isMachine)
        XCTAssertEqual(second.kind, .welcome,
                       "A second window must not inherit the first's consumed target")
    }

    /// Creating the role also clears the pending fields on the shared state, so
    /// nothing is left behind for `consumePendingTarget` to hand out again.
    func testCreatingMachineRoleClearsPendingState() {
        let state = ConnectWindowState.shared
        state.pendingTarget = ConnectionTarget(host: "h", port: 1234)
        state.pendingNeedsRun = true
        state.pendingProvenanceUUID = "p"
        state.pendingShowSidebar = true

        _ = MainWindowRole()

        XCTAssertNil(state.pendingTarget)
        XCTAssertFalse(state.pendingNeedsRun)
        XCTAssertNil(state.pendingProvenanceUUID)
        XCTAssertNil(state.pendingShowSidebar)
    }
}
