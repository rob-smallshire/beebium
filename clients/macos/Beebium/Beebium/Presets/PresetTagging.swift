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

/// Spike (#TBD): client-side tags over the preset catalogue, so the New Machine
/// presets page can offer filter chips. This derives a preset's tags from its
/// `list-presets` model plus the raw preset file -- entirely in the client, with
/// no server, proto or spec change -- so Rob can drive the filters and we can see
/// what the derivation decided. The server will own this classification later, so
/// it is kept here, in one small type, and not spread through the UI: swap this
/// file's `PresetTagger` for a server-supplied tag list and nothing else moves.
///
/// The rules mirror what a person reads off the preset: its family (Model B /
/// B+ / Master), the Model B expansion board, any Tube coprocessor, Econet, the
/// Music 5000, and the disc controller. They are deliberately simple and will not
/// survive contact with every future preset; that is acceptable for a spike.

/// A group of related tags. Chips within a group OR together; groups AND together.
enum PresetTagGroup: String, CaseIterable, Comparable, Sendable {
    case family = "Family"
    case board = "Board"
    case coprocessor = "Coprocessor"
    case econet = "Econet"
    case music = "Music 5000"
    case disc = "Disc"

    /// The order groups appear in the filter bar (declaration order).
    private var order: Int { Self.allCases.firstIndex(of: self) ?? 0 }
    static func < (lhs: PresetTagGroup, rhs: PresetTagGroup) -> Bool {
        lhs.order < rhs.order
    }
}

/// One tag: a value within a group (e.g. Family / "Model B+").
struct PresetTag: Hashable, Comparable, Sendable {
    let group: PresetTagGroup
    let value: String

    static func < (lhs: PresetTag, rhs: PresetTag) -> Bool {
        lhs.group == rhs.group ? lhs.value < rhs.value : lhs.group < rhs.group
    }
}

/// Derives a preset's tags. Pure over its inputs so it is testable without a
/// server: `model` is the `list-presets` row's model (falling back to the file's
/// own `model`), and `preset` is the preset file parsed as JSON.
enum PresetTagger {
    static func tags(model: String?, preset: [String: Any]) -> [PresetTag] {
        let model = (model ?? preset["model"] as? String) ?? ""
        var tags: [PresetTag] = []

        if let family = family(forModel: model) {
            tags.append(PresetTag(group: .family, value: family))
        }
        if let board = board(forModel: model) {
            tags.append(PresetTag(group: .board, value: board))
        }

        let extensionNames = self.extensionNames(in: preset)
        for name in extensionNames {
            if let copro = coprocessor(forExtension: name) {
                tags.append(PresetTag(group: .coprocessor, value: copro))
            }
            if name == musicExtension {
                tags.append(PresetTag(group: .music, value: "Music 5000"))
            }
        }

        if let econet = econet(in: preset, extensionNames: extensionNames) {
            for value in econet { tags.append(PresetTag(group: .econet, value: value)) }
        }

        tags.append(PresetTag(group: .disc, value: disc(in: preset, model: model)))

        // Canonical order and no duplicates, so the chip bar and the test read the
        // same list however the preset happened to spell it.
        var seen = Set<PresetTag>()
        return tags.filter { seen.insert($0).inserted }.sorted()
    }

    // MARK: - Family and board (from the model)

    static func family(forModel model: String) -> String? {
        if model == "master-128" { return "Master 128" }
        if model.hasPrefix("model-b-plus") { return "Model B+" }
        if model.hasPrefix("model-b") { return "Model B" }
        return nil
    }

    /// The expansion board of a Model B variant; nil for a plain Model B, a B+ or
    /// the Master (whose extra memory is part of the model, not a board).
    static func board(forModel model: String) -> String? {
        switch model {
        case "model-b-integra-b":        return "Integra-B"
        case "model-b-atpl-sidewise":    return "ATPL Sidewise"
        case "model-b-watford-rom-ram":  return "Watford ROM/RAM"
        case "model-b-romram":           return "ROM/RAM board"
        default:                         return nil
        }
    }

    // MARK: - Extensions

    private static let musicExtension = "hybrid-music-5000"

    private static func extensionNames(in preset: [String: Any]) -> [String] {
        guard let extensions = preset["extensions"] as? [[String: Any]] else { return [] }
        return extensions.compactMap { $0["name"] as? String }
    }

    /// A coprocessor's short name, or nil for a non-coprocessor extension. The two
    /// bundled Tubes get friendly names; any other coprocessor shows its raw name.
    static func coprocessor(forExtension name: String) -> String? {
        switch name {
        case "acorn-65c02-coprocessor":  return "65C02"
        case "acorn-65c102-coprocessor": return "65C102"
        default:
            return name.contains("coprocessor") ? name : nil
        }
    }

    // MARK: - Econet

    /// Econet values: the transport (AUN / Piconet) and, when the machine fits an
    /// Acorn SCSI host (a file server's disc), "File server".
    private static func econet(in preset: [String: Any], extensionNames: [String]) -> [String]? {
        var values: [String] = []
        if let econet = preset["econet"] as? [String: Any],
           let transport = econet["transport"] as? [String: Any],
           let name = transport["name"] as? String {
            switch name {
            case "aun":     values.append("AUN")
            case "piconet": values.append("Piconet")
            default:        break
            }
        }
        if extensionNames.contains("acorn-scsi") {
            values.append("File server")
        }
        return values.isEmpty ? nil : values
    }

    // MARK: - Disc

    /// The disc controller, or "No disc" when the preset fits no storage. A preset
    /// with storage but no explicit FDC takes the model's built-in controller
    /// (the B+ and Master are 1770); none of the bundled 8271 presets leave it
    /// implicit, so the implicit case is 1770.
    static func disc(in preset: [String: Any], model: String) -> String {
        guard let storage = preset["storage"] as? [String: Any] else { return "No disc" }
        if let fdc = storage["fdc_socket"] as? [String: Any], let id = fdc["id"] as? String {
            switch id {
            case "acorn-8271": return "8271"
            case "acorn-1770": return "1770"
            // A socket fitted with no controller ("none") is, to a user picking a
            // machine, no disc. Anything else unrecognised is treated the same.
            default:           return "No disc"
            }
        }
        return "1770"
    }
}
