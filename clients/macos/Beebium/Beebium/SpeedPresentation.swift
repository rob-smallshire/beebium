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

/// Shared vocabulary for emulation-speed multipliers, so the runtime
/// SpeedControlView (a log2 slider) and the pre-launch Processor speed picker
/// label and choose speeds the same way instead of each having its own.
enum SpeedPresentation {
    /// The preset's/wire token for running flat out.
    static let unlimitedToken = "unlimited"

    /// A multiplier's label: "2×", "1/2", "1×", or a general "1.5×".
    static func label(forMultiplier m: Double) -> String {
        if m >= 1 {
            return m == m.rounded() ? "\(Int(m))×" : String(format: "%g×", m)
        }
        let inverse = 1.0 / m
        return inverse == inverse.rounded() ? "1/\(Int(inverse))" : String(format: "%g×", m)
    }

    /// The selectable speeds offered in the pre-launch picker (plus Unlimited).
    static let presetMultipliers: [Double] = [0.25, 0.5, 1, 2, 4, 8]
}
