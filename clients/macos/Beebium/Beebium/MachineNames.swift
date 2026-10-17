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

/// Ordinals for the machines the app starts, so that several windows built from
/// one preset can be told apart at a glance.
///
/// A machine's name is per instance, not part of a preset: two machines built
/// from one preset are still two machines. The ordinal is what distinguishes
/// them -- the app supplies it with `--machine-ordinal` and the server appends
/// it to the default name as "#2".
///
/// Counting is per preset, and per run of the app. Numbers are never reused
/// within a session -- closing #1 does not free the number for the next
/// machine, because a name that has been on screen should not come back on a
/// different machine -- and never persisted, so a fresh launch starts at #1
/// again.
@MainActor
final class MachineNameSequence {
    private var issued: [String: Int] = [:]

    /// The next ordinal for a machine built from this preset.
    func nextOrdinal(forPreset presetName: String) -> Int {
        let ordinal = (issued[presetName] ?? 0) + 1
        issued[presetName] = ordinal
        return ordinal
    }
}
