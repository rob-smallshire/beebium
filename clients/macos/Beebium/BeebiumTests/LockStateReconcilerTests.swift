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

/// Issue #73: after a machine reset (BREAK etc.) the MOS re-inits the lock latches
/// to CAPS on / SHIFT off, but the app's mirror stayed stale. These tests pin the
/// reconciler that fixes it: the emulated machine is authoritative and the mirror
/// follows it, for both Caps and Shift Lock.
@MainActor
final class LockStateReconcilerTests: XCTestCase {

    /// The reproduction: the user has SHIFT LOCK engaged (caps off, shift on),
    /// then a reset re-inits to caps on / shift off. The mirror must adopt the
    /// machine's fresh state -- before the fix it kept the pre-reset value and
    /// the app applied a stale lock (#73). Covers both locks in one transition.
    func testResetTransitionAdoptsMachineStateForBothLocks() {
        // Pre-reset: SHIFT LOCK engaged, CAPS off (as _engage_shift_lock_caps_off
        // sets up in the server-side characterisation test).
        let reconciler = LockStateReconciler(
            lockState: LockState(capsLockOn: false, shiftLockOn: true))

        // The machine reset re-inits to the power-on default (proven server-side
        // in test_shift_lock_break_reinit.py): CAPS on, SHIFT off.
        let afterReset = LockState(capsLockOn: true, shiftLockOn: false)

        // Reproduction: the mirror is stale relative to the machine.
        XCTAssertNotEqual(reconciler.lockState, afterReset)

        // The fix: adopt the machine's state. Both latches flip in one step, and
        // adopting a stale mirror reports that it corrected something.
        XCTAssertTrue(reconciler.adoptEmulated(afterReset))
        XCTAssertEqual(reconciler.lockState, afterReset)
        XCTAssertTrue(reconciler.lockState.capsLockOn)
        XCTAssertFalse(reconciler.lockState.shiftLockOn)
    }

    /// A Caps-only change (e.g. the user's host Caps key toggled the machine, or
    /// the machine changed caps alone) is mirrored without disturbing shift.
    func testAdoptMirrorsCapsChangeAlone() {
        let reconciler = LockStateReconciler(
            lockState: LockState(capsLockOn: true, shiftLockOn: false))

        XCTAssertTrue(reconciler.adoptEmulated(
            LockState(capsLockOn: false, shiftLockOn: false)))
        XCTAssertFalse(reconciler.lockState.capsLockOn)
        XCTAssertFalse(reconciler.lockState.shiftLockOn)
    }

    /// A Shift-only change is mirrored without disturbing caps.
    func testAdoptMirrorsShiftChangeAlone() {
        let reconciler = LockStateReconciler(
            lockState: LockState(capsLockOn: true, shiftLockOn: false))

        XCTAssertTrue(reconciler.adoptEmulated(
            LockState(capsLockOn: true, shiftLockOn: true)))
        XCTAssertTrue(reconciler.lockState.capsLockOn)
        XCTAssertTrue(reconciler.lockState.shiftLockOn)
    }

    /// Adopting the state the mirror already holds is a no-op and reports so, so
    /// the steady stream of unchanged LED updates does not churn observers.
    func testAdoptingUnchangedStateIsANoOp() {
        let state = LockState(capsLockOn: true, shiftLockOn: false)
        let reconciler = LockStateReconciler(lockState: state)

        XCTAssertFalse(reconciler.adoptEmulated(state))
        XCTAssertEqual(reconciler.lockState, state)
    }

    /// The default mirror before any machine state is known is both-off; the
    /// first adopt (initial GetLockState / first LED) establishes the truth.
    func testInitialMirrorIsBothOffThenAdopts() {
        let reconciler = LockStateReconciler()
        XCTAssertEqual(reconciler.lockState,
                       LockState(capsLockOn: false, shiftLockOn: false))

        XCTAssertTrue(reconciler.adoptEmulated(
            LockState(capsLockOn: true, shiftLockOn: false)))
        XCTAssertEqual(reconciler.lockState,
                       LockState(capsLockOn: true, shiftLockOn: false))
    }
}
