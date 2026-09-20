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

/// Issue #73, Caps Lock half: the host -> guest caps sync, restored and driven by
/// the reset trigger. These pin the pure decision so the resync re-applies the
/// host caps after a reset (boot or Break) without fighting the MOS.
final class LockSyncPolicyTests: XCTestCase {

    // MARK: - The reset-trigger caps re-apply (host -> guest)

    func testTapsWhenHostAndGuestDisagreeAndEnabled() {
        // After a reset, the MOS leaves caps ON; if the host has caps OFF and the
        // feature is on, the resync must tap the guest to re-apply the host state.
        XCTAssertTrue(LockSyncPolicy.shouldTapGuest(
            hostSynced: true, syncEnabled: true, hostOn: false, guestOn: true))
        // And the mirror-image: host ON, guest OFF.
        XCTAssertTrue(LockSyncPolicy.shouldTapGuest(
            hostSynced: true, syncEnabled: true, hostOn: true, guestOn: false))
    }

    func testDoesNotTapWhenAlreadyAligned() {
        // The reset resync runs after the settle, so the guest may already match
        // the host (e.g. host caps ON, MOS reset caps ON). It must NOT tap then,
        // which is what keeps it from fighting the MOS.
        XCTAssertFalse(LockSyncPolicy.shouldTapGuest(
            hostSynced: true, syncEnabled: true, hostOn: true, guestOn: true))
        XCTAssertFalse(LockSyncPolicy.shouldTapGuest(
            hostSynced: true, syncEnabled: true, hostOn: false, guestOn: false))
    }

    func testDoesNotTapWhenFeatureDisabled() {
        XCTAssertFalse(LockSyncPolicy.shouldTapGuest(
            hostSynced: true, syncEnabled: false, hostOn: true, guestOn: false))
    }

    func testDoesNotTapWhenLockIsNotHostSynced() {
        // Shift Lock on macOS is guest-only: even if host and guest "disagree",
        // a non-host-synced lock is never driven from the host.
        XCTAssertFalse(LockSyncPolicy.shouldTapGuest(
            hostSynced: false, syncEnabled: true, hostOn: true, guestOn: false))
    }

    // MARK: - Per-platform policy

    func testMacOSPolicyHostSyncsCapsButNotShift() {
        let policy = LockSyncPolicy.current
        #if os(macOS)
        XCTAssertTrue(policy.capsIsHostSynced, "macOS host-syncs Caps Lock")
        XCTAssertFalse(policy.shiftIsHostSynced, "macOS has no host Shift Lock")
        #endif
    }
}
