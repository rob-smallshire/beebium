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

/// Names for machines the app starts, so that several windows can be told
/// apart at a glance.
///
/// A machine's name is per instance, not part of a preset: two machines built
/// from one preset are still two machines. Left to itself the server names a
/// machine after its model, so every Model B would be called the same thing
/// and the windows would be indistinguishable -- which is what this exists to
/// prevent.
///
/// The name is the preset's, with an ordinal: "BBC Model B #1",
/// "BBC Model B #2", "BBC Model B (Disc) #1". It says what a machine is as
/// well as which one it is.
///
/// Counting is per preset, and per run of the app. Numbers are never reused
/// within a session -- closing #1 does not free the number for the next
/// machine, because a name that has been on screen should not come back on a
/// different machine -- and never persisted, so a fresh launch starts at #1
/// again.
@MainActor
final class MachineNameSequence {
    private var issued: [String: Int] = [:]

    /// The next name for a machine built from this preset.
    func next(forPreset presetName: String) -> String {
        let ordinal = (issued[presetName] ?? 0) + 1
        issued[presetName] = ordinal
        return "\(presetName) #\(ordinal)"
    }
}
