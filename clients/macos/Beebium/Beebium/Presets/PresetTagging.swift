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

/// Spike: client-side tags over the preset catalogue, so the New Machine presets
/// page can offer filter popups. This derives a preset's tags from its
/// `list-presets` model, the raw preset file, and the model's storage schema --
/// entirely in the client, with no server, proto or spec change -- so Rob can
/// drive the filters and we can see what the derivation decided. The server will
/// own this classification later, so it is kept here, in one small type, and not
/// spread through the UI: swap `PresetTagger` for a server-supplied tag list and
/// nothing else moves.
///
/// The groups are named for the sidebar / configurator modes (machine, storage,
/// peripherals, processor, network), so the model, the popups and the tests all
/// speak the same words. The rules are deliberately simple and will not survive
/// contact with every future preset; that is acceptable for a spike.

/// A group of related tags, one per filter facet. Named and ordered after the
/// sidebar modes (the configurator's section order), with the model first. It is
/// "model", not "machine", to avoid confusion with the machine instances that
/// also appear in the Welcome window.
enum PresetTagGroup: String, CaseIterable, Comparable, Sendable {
    case model
    case storage
    case peripherals
    case processor
    case network

    /// The order groups sort and appear in (declaration order).
    private var order: Int { Self.allCases.firstIndex(of: self) ?? 0 }
    static func < (lhs: PresetTagGroup, rhs: PresetTagGroup) -> Bool {
        lhs.order < rhs.order
    }
}

/// One tag: a value within a group (e.g. machine / "Model B+").
struct PresetTag: Hashable, Comparable, Sendable {
    let group: PresetTagGroup
    let value: String

    static func < (lhs: PresetTag, rhs: PresetTag) -> Bool {
        lhs.group == rhs.group ? lhs.value < rhs.value : lhs.group < rhs.group
    }
}

/// Derives a preset's tags. Pure over its inputs so it is testable without a
/// server: `model` is the `list-presets` row's model (falling back to the file's
/// own `model`), `preset` is the preset file parsed as JSON, and
/// `builtinFdcDevice` is the model's soldered disc controller from its storage
/// schema (`builtin.fdc.device`, e.g. "WD1770"), nil when the model has none.
enum PresetTagger {
    static func tags(model: String?, preset: [String: Any],
                     builtinFdcDevice: String?) -> [PresetTag] {
        let model = (model ?? preset["model"] as? String) ?? ""
        var tags: [PresetTag] = []

        if let family = family(forModel: model) {
            tags.append(PresetTag(group: .model, value: family))
        }

        tags.append(PresetTag(group: .storage,
                              value: storage(in: preset, builtinFdcDevice: builtinFdcDevice)))

        for name in extensionNames(in: preset) {
            if let processor = processor(forExtension: name) {
                tags.append(PresetTag(group: .processor, value: processor))
            } else if let peripheral = peripheral(forExtension: name) {
                tags.append(PresetTag(group: .peripherals, value: peripheral))
            }
        }

        if let network = network(in: preset) {
            tags.append(PresetTag(group: .network, value: network))
        }

        // Canonical order and no duplicates, so the popups and the tests read the
        // same list however the preset happened to spell it.
        var seen = Set<PresetTag>()
        return tags.filter { seen.insert($0).inserted }.sorted()
    }

    // MARK: - Model family (from the model id)

    static func family(forModel model: String) -> String? {
        if model == "master-128" { return "Master 128" }
        if model.hasPrefix("model-b-plus") { return "Model B+" }
        if model.hasPrefix("model-b") { return "Model B" }
        return nil
    }

    // MARK: - Storage

    /// The disc controller: the fitted socket's controller when the preset fits
    /// one, else the model's soldered built-in controller from its schema, else
    /// "No disc". The server states the built-in where it belongs (the Master and
    /// B+ family carry `builtin.fdc` because they have no socket), so this never
    /// hard-codes a model.
    static func storage(in preset: [String: Any], builtinFdcDevice: String?) -> String {
        if let storage = preset["storage"] as? [String: Any],
           let fdc = storage["fdc_socket"] as? [String: Any],
           let id = fdc["id"] as? String {
            switch id {
            case "acorn-8271": return "8271"
            case "acorn-1770": return "1770"
            default:           return "No disc"   // "none", or an unknown controller
            }
        }
        if let device = builtinFdcDevice { return controller(forDevice: device) }
        return "No disc"
    }

    /// Map a controller device name (e.g. "WD1770", "Intel8271") to the tag value.
    static func controller(forDevice device: String) -> String {
        let device = device.lowercased()
        if device.contains("1770") { return "1770" }
        if device.contains("8271") { return "8271" }
        return "No disc"
    }

    // MARK: - Extensions (processor and peripherals)

    private static func extensionNames(in preset: [String: Any]) -> [String] {
        guard let extensions = preset["extensions"] as? [[String: Any]] else { return [] }
        return extensions.compactMap { $0["name"] as? String }
    }

    /// A coprocessor's short name, or nil for a non-coprocessor extension. The two
    /// bundled Tubes get friendly names; any other coprocessor shows its raw name.
    static func processor(forExtension name: String) -> String? {
        switch name {
        case "acorn-65c02-coprocessor":  return "65C02"
        case "acorn-65c102-coprocessor": return "65C102"
        default:
            return name.contains("coprocessor") ? name : nil
        }
    }

    /// Extensions that are plumbing for another facet (a file server's SCSI host
    /// and disc, a real-time clock) rather than a peripheral a person would pick.
    private static let infrastructureExtensions: Set<String> = [
        "acorn-scsi", "scsi-hard-disc", "acorn-rtc",
    ]

    /// A peripheral's name, or nil for a coprocessor (handled above) or
    /// infrastructure. The Music 5000 gets a friendly name; any other peripheral
    /// extension shows by its raw name, so the facet extends itself.
    static func peripheral(forExtension name: String) -> String? {
        if name == "hybrid-music-5000" { return "Music 5000" }
        if infrastructureExtensions.contains(name) { return nil }
        return name
    }

    // MARK: - Network

    /// The Econet transport (AUN / Piconet), or nil. A file server runs *over* a
    /// transport, so it is not a separate value -- the preset is tagged by its
    /// transport alone.
    static func network(in preset: [String: Any]) -> String? {
        guard let econet = preset["econet"] as? [String: Any],
              let transport = econet["transport"] as? [String: Any],
              let name = transport["name"] as? String else { return nil }
        switch name {
        case "aun":     return "AUN"
        case "piconet": return "Piconet"
        default:        return nil
        }
    }
}
