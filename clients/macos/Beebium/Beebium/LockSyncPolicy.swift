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

/// Which lock keys the host keyboard drives into the guest, per platform.
///
/// The two BBC locks need different treatment (issue #73):
/// - **Caps Lock** is host → guest: when the feature is enabled, the app aligns
///   the guest's caps latch to the host keyboard's Caps Lock. The host caps
///   cannot be driven or aligned from the guest, so this direction is the only
///   one that exists.
/// - **Shift Lock** has no host equivalent on macOS, so it is guest-only: the
///   app merely reflects the guest's shift latch (see `LockStateReconciler`).
///
/// This is deliberately a per-platform, per-lock policy rather than hardcoding
/// "shift is always guest-only": a Linux/Windows front-end has a physical Shift
/// Lock and would host-sync it too.
struct LockSyncPolicy: Equatable {
    /// Whether the host keyboard's Caps Lock drives the guest's caps latch.
    let capsIsHostSynced: Bool
    /// Whether the host keyboard's Shift Lock drives the guest's shift latch.
    let shiftIsHostSynced: Bool

    /// The policy for the platform this build runs on. macOS: caps host-synced,
    /// shift guest-only. Other hosts have a physical Shift Lock, so both.
    static var current: LockSyncPolicy {
        #if os(macOS)
        return LockSyncPolicy(capsIsHostSynced: true, shiftIsHostSynced: false)
        #else
        return LockSyncPolicy(capsIsHostSynced: true, shiftIsHostSynced: true)
        #endif
    }

    /// Whether to tap the guest lock to align it to the host, given the feature
    /// toggle and the current host/guest states.
    ///
    /// Pure so the resync path is unit-testable. It taps only when the lock is
    /// host-synced on this platform, the feature is enabled, and the host and
    /// guest actually disagree -- so a resync that runs after the settle delay
    /// never taps a lock the MOS has already left in the wanted state, and so
    /// cannot fight the MOS's re-initialisation.
    static func shouldTapGuest(hostSynced: Bool, syncEnabled: Bool,
                               hostOn: Bool, guestOn: Bool) -> Bool {
        hostSynced && syncEnabled && hostOn != guestOn
    }
}
