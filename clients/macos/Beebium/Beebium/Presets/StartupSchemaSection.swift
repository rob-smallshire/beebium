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

/// The `startup` section of describe-preset-schema: the startup keyboard links,
/// expressed either as a screen mode plus auto-boot, or as the whole links byte
/// (the two forms are mutually exclusive).
///
/// The `links` layout describes the byte itself -- which bits are the screen
/// mode, which is the SHIFT+BREAK/auto-boot bit, and a label per link -- so the
/// Keyboard section can present the friendly controls, the eight link check
/// boxes, and the hex and decimal fields as four live projections of one byte,
/// with no BBC bit knowledge hard-coded in the client.
struct StartupSchemaSection: Codable {
    let type: String   // "startup"
    let screenMode: IntRange
    let autoBoot: BoolDefault
    let links: LinksLayout

    struct IntRange: Codable {
        let min: Int
        let max: Int
        /// Present for screen_mode; absent for links.
        let `default`: Int?
    }

    struct BoolDefault: Codable {
        let `default`: Bool
    }

    /// The layout of the startup links byte.
    struct LinksLayout: Codable {
        let min: Int
        let max: Int
        /// The byte a fresh machine reads (all links open): the friendly form's
        /// starting point and the baseline the non-mode/boot bits are compared to.
        let `default`: Int
        /// "clear" or "set": a link is made (fitted) when its bit is in this
        /// state. BBC links are active-low, so this is "clear".
        var linkMadeWhen: String = "clear"
        /// The bits holding the screen mode, least-significant first.
        var screenModeBits: [Int] = []
        /// The bit holding the SHIFT+BREAK/auto-boot option, if any.
        var autoBootBit: Int? = nil
        /// "clear" or "set": auto_boot is true when auto_boot_bit is in this state.
        var autoBootTrueWhen: String = "clear"
        /// One entry per link, most-significant bit first, naming and describing it.
        var bits: [LinkBit] = []

        enum CodingKeys: String, CodingKey {
            case min, max, bits
            case `default`
            case linkMadeWhen = "link_made_when"
            case screenModeBits = "screen_mode_bits"
            case autoBootBit = "auto_boot_bit"
            case autoBootTrueWhen = "auto_boot_true_when"
        }
    }

    /// One link in the startup byte.
    struct LinkBit: Codable, Identifiable {
        let bit: Int
        var link: String = ""
        var label: String = ""
        var description: String = ""

        var id: Int { bit }

        enum CodingKeys: String, CodingKey {
            case bit, link, label, description
        }

        // Only `bit` is required; the rest tolerate absence.
        init(from decoder: Decoder) throws {
            let c = try decoder.container(keyedBy: CodingKeys.self)
            bit = try c.decode(Int.self, forKey: .bit)
            link = try c.decodeIfPresent(String.self, forKey: .link) ?? ""
            label = try c.decodeIfPresent(String.self, forKey: .label) ?? ""
            description = try c.decodeIfPresent(String.self, forKey: .description) ?? ""
        }
    }

    enum CodingKeys: String, CodingKey {
        case type, links
        case screenMode = "screen_mode"
        case autoBoot = "auto_boot"
    }
}

// MARK: - Links byte projections

/// Reading and writing the startup links byte through the schema's layout -- the
/// one place the byte's structure is interpreted, all of it driven by the schema
/// so no BBC bit semantics live in the client. A byte is an Int in 0...255.
extension StartupSchemaSection.LinksLayout {
    /// Whether a link's bit being set counts as "made" (fitted).
    private var madeMeansSet: Bool { linkMadeWhen == "set" }

    /// The screen mode encoded in `byte` (its screen-mode bits, LSB first).
    func screenMode(fromByte byte: Int) -> Int {
        var value = 0
        for (index, bit) in screenModeBits.enumerated() where byte & (1 << bit) != 0 {
            value |= (1 << index)
        }
        return value
    }

    /// `byte` with its screen-mode bits set to `mode`.
    func byte(_ byte: Int, settingScreenMode mode: Int) -> Int {
        var result = byte
        for (index, bit) in screenModeBits.enumerated() {
            if mode & (1 << index) != 0 { result |= (1 << bit) } else { result &= ~(1 << bit) }
        }
        return result
    }

    /// Whether auto_boot is on in `byte`.
    func autoBoot(fromByte byte: Int) -> Bool {
        guard let autoBootBit = autoBootBit else { return false }
        let bitSet = byte & (1 << autoBootBit) != 0
        return autoBootTrueWhen == "set" ? bitSet : !bitSet
    }

    /// `byte` with its auto_boot bit set to represent `on`.
    func byte(_ byte: Int, settingAutoBoot on: Bool) -> Int {
        guard let autoBootBit = autoBootBit else { return byte }
        let wantSet = (autoBootTrueWhen == "set") ? on : !on
        return wantSet ? (byte | (1 << autoBootBit)) : (byte & ~(1 << autoBootBit))
    }

    /// The bits that are neither screen mode nor auto_boot.
    var otherBitsMask: Int {
        var mask = 0xFF
        for bit in screenModeBits { mask &= ~(1 << bit) }
        if let autoBootBit = autoBootBit { mask &= ~(1 << autoBootBit) }
        return mask
    }

    /// Whether `byte` can be written as the friendly screen_mode/auto_boot form
    /// without loss: only when every other bit is at its default.
    func isFriendlyRepresentable(_ byte: Int) -> Bool {
        (byte & otherBitsMask) == (`default` & otherBitsMask)
    }

    /// The byte for a friendly screen_mode/auto_boot pair: the default, with the
    /// mode and auto_boot applied (so the other bits keep their default state).
    func byte(screenMode mode: Int, autoBoot: Bool) -> Int {
        byte(byte(`default`, settingScreenMode: mode), settingAutoBoot: autoBoot)
    }

    /// Whether the link at `bit` reads as made (fitted) in `byte`.
    func isLinkMade(bit: Int, in byte: Int) -> Bool {
        let bitSet = byte & (1 << bit) != 0
        return madeMeansSet ? bitSet : !bitSet
    }

    /// `byte` with the link at `bit` made or broken.
    func byte(_ byte: Int, settingLink bit: Int, made: Bool) -> Int {
        let wantSet = madeMeansSet ? made : !made
        return wantSet ? (byte | (1 << bit)) : (byte & ~(1 << bit))
    }
}
