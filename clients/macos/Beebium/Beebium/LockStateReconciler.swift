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

import Foundation

/// The BBC's two lock latches, CAPS LOCK and SHIFT LOCK.
struct LockState: Equatable, Sendable {
    var capsLockOn: Bool
    var shiftLockOn: Bool
}

/// The app's mirror of the emulated machine's CAPS/SHIFT LOCK state (issue #73).
///
/// The emulated machine is **authoritative**: it owns the lock latches and
/// re-initialises them on every reset (BREAK, Ctrl-BREAK, crash-then-BREAK) to
/// the power-on default of CAPS LOCK on / SHIFT LOCK off. The macOS app used to
/// drive the machine's caps latch to match the host keyboard and only reconciled
/// on launch timers / the first LED / window focus, so after a reset the app kept
/// applying its stale idea of the lock state and fought the MOS -- the #73 defect
/// (letters coming out lower case, or shift-mangled input, until the user cycled
/// the lock by hand).
///
/// The fix is to mirror the machine continuously. The `caps-lock-led` /
/// `shift-lock-led` indicators change exactly when the MOS re-inits on reset, so
/// they are a race-free push signal to re-read the exact latch (`GetLockState`)
/// and adopt it here. A reset is then just another lock change the app follows;
/// nothing is injected back into the machine for a machine-driven change (the
/// host key still toggles the machine on the way in -- see KeyboardClient).
///
/// The reconcile decision is deliberately a plain, observable class rather than
/// SwiftUI callbacks, so the reset transition is unit-testable.
@MainActor
final class LockStateReconciler: ObservableObject {

    /// The app's current belief about the machine's lock latches.
    @Published private(set) var lockState: LockState

    init(lockState: LockState = LockState(capsLockOn: false, shiftLockOn: false)) {
        self.lockState = lockState
    }

    /// Adopt the machine's reported lock state. The machine is authoritative, so
    /// the app simply follows it -- a reset that re-inits CAPS on / SHIFT off is
    /// just another change to mirror, and no key is tapped back into the machine.
    ///
    /// - Returns: `true` if the mirror was stale and has now been corrected
    ///   (useful for tests and logging); `false` if it already matched.
    @discardableResult
    func adoptEmulated(_ emulated: LockState) -> Bool {
        guard lockState != emulated else { return false }
        lockState = emulated
        return true
    }
}
