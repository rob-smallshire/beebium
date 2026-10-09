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

/// The `econet` section of describe-preset-schema: the station-number rules and
/// the transports the model can use. Present only on a model with an Econet
/// socket, which is what gates the Network section.
struct EconetSchemaSection: Codable {
    let type: String   // "econet"
    let station: StationRules
    /// Transport names (extensions of kind econet-transport), e.g. aun, piconet.
    let transports: [String]

    /// The rules for the station number.
    struct StationRules: Codable {
        let min: Int
        let max: Int
        /// Whether "auto" (and "auto:lo-hi") are permitted.
        let auto: Bool
        /// The default [lo, hi] for an auto range.
        let autoDefaultRange: [Int]

        var autoDefaultLow: Int { autoDefaultRange.first ?? min }
        var autoDefaultHigh: Int { autoDefaultRange.count > 1 ? autoDefaultRange[1] : max }

        enum CodingKeys: String, CodingKey {
            case min, max, auto
            case autoDefaultRange = "auto_default_range"
        }
    }
}

/// The three forms an Econet station takes, parsed from and rendered to the
/// draft's `econet.station` value (an integer, "auto", or "auto:lo-hi"). Kept
/// pure so the parsing/formatting is testable without the view.
enum EconetStation: Equatable {
    case number(Int)
    case auto
    case autoRange(Int, Int)

    /// Parse a station value; an integer is a fixed number, "auto" is automatic,
    /// "auto:lo-hi" is an automatic range. Anything else (incl. a non-integer
    /// or a malformed range) is treated as a fixed number of 0, which the
    /// server then flags.
    init(_ value: JSONValue?) {
        switch value {
        case .int(let n):
            self = .number(n)
        case .string(let s) where s == "auto":
            self = .auto
        case .string(let s) where s.hasPrefix("auto:"):
            let parts = s.dropFirst("auto:".count).split(separator: "-")
            if parts.count == 2, let lo = Int(parts[0]), let hi = Int(parts[1]) {
                self = .autoRange(lo, hi)
            } else {
                self = .auto
            }
        default:
            self = .number(0)
        }
    }

    /// The draft value: an integer for a fixed number, a string for auto forms.
    var jsonValue: JSONValue {
        switch self {
        case .number(let n):        return .int(n)
        case .auto:                 return .string("auto")
        case .autoRange(let lo, let hi): return .string("auto:\(lo)-\(hi)")
        }
    }
}
