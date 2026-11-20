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

/// Spike shim: client-side facet tags over the preset catalogue, so the New
/// Machine presets page can offer the filter row (client-specification.md 5.1)
/// before the server publishes facets. It derives a preset's facets from its
/// `list-presets` model, the raw preset file, and the model's storage schema.
///
/// This whole file is temporary. When the server lands -- each `list-presets`
/// row carrying a `facets` object and `describe-facets` publishing the ordered
/// facet list -- `PresetTagger`, the bundle read and the per-model schema fetch
/// are deleted and `PresetFilterModel` binds to the rows. The derivation is kept
/// here in one type so that is a deletion, not a rewrite.
///
/// The facets match the spec: four exclusive facets (Model, Coprocessor, Disc
/// controller, Network), each giving a preset exactly one value, and two set
/// facets (Storage, Peripherals), where a preset carries several values or none.

/// A facet's tag group. Ordered to match the spec's facet order.
enum PresetTagGroup: String, CaseIterable, Comparable, Sendable {
    case model
    case coprocessor
    case discController
    case network
    case storage
    case peripherals

    /// The order groups sort and appear in (declaration order).
    private var order: Int { Self.allCases.firstIndex(of: self) ?? 0 }
    static func < (lhs: PresetTagGroup, rhs: PresetTagGroup) -> Bool {
        lhs.order < rhs.order
    }
}

/// One tag: a value within a group (e.g. model / "Model B+").
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

        let extensionNames = self.extensionNames(in: preset)
        for name in extensionNames {
            if let coprocessor = coprocessor(forExtension: name) {
                tags.append(PresetTag(group: .coprocessor, value: coprocessor))
            } else if let peripheral = peripheral(forExtension: name) {
                tags.append(PresetTag(group: .peripherals, value: peripheral))
            }
        }

        let disc = discController(in: preset, builtinFdcDevice: builtinFdcDevice)
        tags.append(PresetTag(group: .discController, value: disc))

        if let network = network(in: preset) {
            tags.append(PresetTag(group: .network, value: network))
        }

        for medium in storageMedia(in: preset, discController: disc, extensionNames: extensionNames) {
            tags.append(PresetTag(group: .storage, value: medium))
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

    // MARK: - Disc controller (exclusive)

    /// The fitted socket's controller when the preset fits one, else the model's
    /// soldered built-in controller from its schema (the Master and B+ family
    /// carry `builtin.fdc`), else "No disc". Never hard-codes a model.
    static func discController(in preset: [String: Any], builtinFdcDevice: String?) -> String {
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

    // MARK: - Network (exclusive)

    /// The Econet transport (AUN / Piconet), or nil. A file server runs *over* a
    /// transport, so it is tagged by its transport, not as a value of its own.
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

    // MARK: - Storage media (set)

    /// What storage media the machine presents: Floppy when it has a disc
    /// controller or configured floppy drives, Hard disc for a SCSI/IDE hard
    /// disc, Cassette for a configured tape. A preset may have several or none.
    static func storageMedia(in preset: [String: Any], discController disc: String,
                             extensionNames: [String]) -> [String] {
        var media: [String] = []
        let floppyDrives = (preset["storage"] as? [String: Any])?["floppy_drives"] as? [[String: Any]]
        if disc != "No disc" || !(floppyDrives?.isEmpty ?? true) {
            media.append("Floppy")
        }
        if extensionNames.contains(where: { $0.contains("hard-disc") }) {
            media.append("Hard disc")
        }
        if hasCassetteTape(in: preset) {
            media.append("Cassette")
        }
        return media
    }

    private static func hasCassetteTape(in preset: [String: Any]) -> Bool {
        if let storage = preset["storage"] as? [String: Any] {
            if let cassette = storage["cassette"] as? [String: Any], cassette["image_uri"] != nil {
                return true
            }
            if storage["cassette"] is String { return true }
        }
        return preset["cassette"] != nil || preset["tape"] != nil
    }

    // MARK: - Extensions (coprocessor and peripherals)

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

    /// A peripheral's name, or nil for a coprocessor (handled above) or a hard
    /// disc (which is storage, not a peripheral). Known peripherals get friendly
    /// names; any other extension shows by its raw name, so the facet extends
    /// itself.
    static func peripheral(forExtension name: String) -> String? {
        switch name {
        case "hybrid-music-5000": return "Music 5000"
        case "acorn-rtc":         return "Real-time clock"
        case "acorn-scsi":        return "SCSI adapter"
        default:
            if name.contains("serial") || name.contains("ip232") { return "Serial" }
            if name.contains("hard-disc") { return nil }   // storage, not a peripheral
            return name
        }
    }
}
