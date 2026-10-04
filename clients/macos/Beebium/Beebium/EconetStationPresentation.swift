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

/// What the Econet station row shows, taken from the number in force and the
/// configured number (#172). The row shows the number in use; when a change has
/// been configured but not yet adopted, it shows the configured number beside it
/// as pending, with a Break action that resolves it. Everything disappears when
/// the two converge -- the status stream pushes the change at the moment the
/// guest adopts the number.
///
/// Pure, so the three cases are testable without a view.
struct EconetStationPresentation: Equatable {
    enum State: Equatable {
        /// The configured number is in force: show it alone.
        case settled(station: UInt32)
        /// A change is configured but not yet adopted: `inForce` is the number
        /// the machine is using now, `configured` the one that takes effect at
        /// the next Break.
        case pending(inForce: UInt32, configured: UInt32)
    }

    let state: State

    /// - Parameters:
    ///   - configured: the configured station number (`station_id`), what the
    ///     edit popover changes.
    ///   - inForce: the number in force (`station_in_force`). Zero means the
    ///     server has not reported one yet, or is too old to carry the field; in
    ///     either case there is nothing pending, so it is treated as settled on
    ///     the configured number rather than showing a spurious "N -> 0".
    init(configured: UInt32, inForce: UInt32) {
        let effectiveInForce = inForce == 0 ? configured : inForce
        if effectiveInForce == configured {
            state = .settled(station: configured)
        } else {
            state = .pending(inForce: effectiveInForce, configured: configured)
        }
    }

    /// Whether a configured change is awaiting its next Break.
    var isPending: Bool {
        if case .pending = state { return true }
        return false
    }

    /// The number in use now, whatever the state.
    var inForceText: String {
        switch state {
        case .settled(let station):       return "\(station)"
        case .pending(let inForce, _):    return "\(inForce)"
        }
    }

    /// The pending suffix shown in the secondary colour after the number in use
    /// ("-> 83"), or "" when nothing is pending. The arrow is a real glyph via a
    /// unicode escape so the source stays 7-bit ASCII.
    var pendingSuffixText: String {
        switch state {
        case .settled:
            return ""
        case .pending(_, let configured):
            return "\u{2192} \(configured)"
        }
    }

    /// The Break button's tooltip, naming the configured number and what Break
    /// does, or nil when nothing is pending (the button is not shown).
    var breakTooltip: String? {
        switch state {
        case .settled:
            return nil
        case .pending(_, let configured):
            return "\(configured) takes effect at the next Break. Click to press Break."
        }
    }
}
