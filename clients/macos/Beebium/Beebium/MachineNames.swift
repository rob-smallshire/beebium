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
/// them -- "#1", "#2" -- and the app writes it into the machine's name template
/// as literal text at launch (see `MachineLaunchName`).
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

/// How the app composes the name template it passes to a machine at launch
/// (`--machine-name`), kept pure so the composition is testable without a
/// launch.
///
/// The template is the preset's own `machine_name` when it has one -- a preset
/// author may write placeholders like `Station {econet-station}` -- otherwise
/// the preset's display name with its braces escaped so a literal name is never
/// read as containing a placeholder. The per-launch ordinal is appended as
/// literal text, since which machine this is belongs to the launcher, not to
/// the machine (docs/discussion/machine-name-templates.md section 8).
enum MachineLaunchName {
    /// Escape `{` and `}` as `{{` and `}}` so a plain name containing a brace
    /// renders verbatim rather than being parsed as a placeholder.
    static func escapingBraces(_ text: String) -> String {
        text.replacingOccurrences(of: "{", with: "{{")
            .replacingOccurrences(of: "}", with: "}}")
    }

    /// The template to launch with: the preset's `machine_name` if non-empty,
    /// otherwise its display name with braces escaped; then " #<ordinal>".
    static func template(presetMachineName: String?,
                         presetDisplayName: String,
                         ordinal: Int) -> String {
        let base: String
        if let machineName = presetMachineName, !machineName.isEmpty {
            base = machineName
        } else {
            base = escapingBraces(presetDisplayName)
        }
        return "\(base) #\(ordinal)"
    }
}
