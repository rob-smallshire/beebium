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

/// A filter facet: one popup over the preset catalogue, backed by a tag group.
/// Each popup is single-selection and defaults to "Any"; selections AND across
/// popups. The facet keeps `PresetTagger`'s vocabulary and only presents it.
enum PresetFacet: String, CaseIterable, Identifiable, Sendable {
    case machine, board, coprocessor, network, disc, addOns

    var id: String { rawValue }

    /// The tag group this facet filters on.
    var group: PresetTagGroup {
        switch self {
        case .machine:     return .family
        case .board:       return .board
        case .coprocessor: return .coprocessor
        case .network:     return .econet
        case .disc:        return .disc
        case .addOns:      return .music
        }
    }

    /// The popup's label.
    var label: String {
        switch self {
        case .machine:     return "Machine"
        case .board:       return "Board"
        case .coprocessor: return "Coprocessor"
        case .network:     return "Network"
        case .disc:        return "Disc"
        case .addOns:      return "Add-ons"
        }
    }

    /// Whether a "None" choice is offered. Every preset has a machine family, so
    /// Machine has no None; the rest can lack their feature.
    var offersNone: Bool { self != .machine }

    /// Whether a preset (its values in this facet's group) counts as "None". For
    /// most facets that is the absence of any tag; for Disc, where every preset
    /// carries a disc tag, "None" is the "No disc" value.
    func isNone(_ values: Set<String>) -> Bool {
        switch self {
        case .disc: return !values.contains("8271") && !values.contains("1770")
        default:    return values.isEmpty
        }
    }

    /// The concrete (non-None) values to offer, from the values present in the
    /// group. Disc hides "No disc" here because it is the None choice.
    func concreteValues(from values: Set<String>) -> [String] {
        switch self {
        case .disc: return values.subtracting(["No disc"]).sorted()
        default:    return values.sorted()
        }
    }
}

/// A single popup's current choice.
enum FacetChoice: Hashable {
    case any
    case none
    case value(String)
}

/// Spike state behind the preset catalogue's filter popups and search field:
/// which tags each preset carries, each popup's choice, and the search text. It
/// reads each preset file once (the system presets are `.beebium-preset` ZIP
/// bundles) to derive its tags via `PresetTagger`, then filters the catalogue.
/// No persistence -- the state lives only as long as the window.
///
/// Popup semantics: each facet is single-selection, defaulting to Any; the
/// choices AND across facets. A choice's count is faceted -- how many presets it
/// would give under the other popups' selections and the search -- so an item
/// that can give nothing reads zero and is disabled (Any is always enabled).
@MainActor
final class PresetFilterModel: ObservableObject {
    @Published var searchText: String = ""
    @Published private(set) var selection: [PresetFacet: FacetChoice] = [:]

    /// Tags per preset id, built by `index`. Published so the catalogue re-renders
    /// once indexing (which reads the bundles off the main actor) completes and
    /// the popups can be built from the real tags.
    @Published private var tagsById: [String: Set<PresetTag>] = [:]
    /// Lower-cased "name description" per preset id, for substring search.
    private var searchTextById: [String: String] = [:]

    /// One selectable choice in a popup, with its faceted count.
    struct Option: Identifiable {
        let choice: FacetChoice
        let label: String
        let count: Int
        let isEnabled: Bool
        var id: FacetChoice { choice }
    }

    /// One popup: its facet, current choice, and options (Any first).
    struct Popup: Identifiable {
        let facet: PresetFacet
        let choice: FacetChoice
        let options: [Option]
        var id: PresetFacet { facet }
    }

    /// Whether any popup is off Any (the Reset affordance shows then).
    var isFiltering: Bool { selection.values.contains { $0 != .any } }

    // MARK: - Indexing

    /// The parts of a preset the indexer needs, pulled out as plain values so the
    /// file reads can run off the main actor.
    private struct Entry: Sendable {
        let id: String
        let filepath: String
        let model: String?
        let search: String
    }

    /// Derive and cache every preset's tags and searchable text. The system
    /// presets are `.beebium-preset` ZIP bundles, so the preset JSON is extracted
    /// from the bundle (off the main actor); this is fine for a local spike, and
    /// the server will supply the tags later. Family and board come from the
    /// row's model, so they survive even when the bundle cannot be read.
    /// Re-indexing the same list is cheap and idempotent.
    func index(_ presets: [MachinePreset]) async {
        let entries = presets.map { preset in
            Entry(id: preset.presetId, filepath: preset.presetFilepath, model: preset.modelId,
                  search: "\(preset.name) \(preset.modelDescription ?? "")".lowercased())
        }
        let derived = await Task.detached { () -> [(String, Set<PresetTag>, String)] in
            entries.map { entry in
                let object = Self.presetObject(atFilepath: entry.filepath)
                let tags = Set(PresetTagger.tags(model: entry.model, preset: object ?? [:]))
                return (entry.id, tags, entry.search)
            }
        }.value

        var tags: [String: Set<PresetTag>] = [:]
        var search: [String: String] = [:]
        for (id, presetTags, searchText) in derived {
            tags[id] = presetTags
            search[id] = searchText
        }
        tagsById = tags
        searchTextById = search
        // Drop any selection the catalogue no longer offers.
        for facet in PresetFacet.allCases {
            if case .value(let v) = selection[facet],
               !allValues(for: facet, in: Array(tags.keys)).contains(v) {
                selection[facet] = .any
            }
        }
    }

    /// The tags derived for a preset id (empty if unknown). For tests.
    func tags(forPresetId id: String) -> Set<PresetTag> { tagsById[id] ?? [] }

    /// Seed tags directly, bypassing the file read. For tests only.
    func loadTagsForTesting(_ tags: [String: [PresetTag]], search: [String: String] = [:]) {
        tagsById = tags.mapValues(Set.init)
        searchTextById = search
    }

    // MARK: - Selection

    func select(_ facet: PresetFacet, _ choice: FacetChoice) {
        selection[facet] = choice
    }

    func choice(for facet: PresetFacet) -> FacetChoice { selection[facet] ?? .any }

    /// Reset every popup to Any (the search field clears itself).
    func reset() {
        selection.removeAll()
    }

    // MARK: - Filtering

    func filtered(_ presets: [MachinePreset]) -> [MachinePreset] {
        presets.filter { matchesSearch($0.presetId) && matchesSelection($0.presetId) }
    }

    /// The popups to show, in facet order. A facet with fewer than two non-Any
    /// options across the whole catalogue (nothing to choose) is omitted, so the
    /// row stays stable as selections change.
    func popups(for presets: [MachinePreset]) -> [Popup] {
        let ids = presets.map(\.presetId)
        return PresetFacet.allCases.compactMap { facet in
            let choices = offeredChoices(for: facet, in: ids)
            guard choices.count >= 2 else { return nil }   // only "Any": nothing to pick
            let current = choice(for: facet)
            let options = ([FacetChoice.any] + choices).map { choice -> Option in
                let count = count(for: facet, choice: choice, in: presets)
                let enabled = choice == .any || count > 0 || choice == current
                return Option(choice: choice, label: label(for: choice, facet: facet, count: count),
                              count: count, isEnabled: enabled)
            }
            return Popup(facet: facet, choice: current, options: options)
        }
    }

    /// The short label for a facet's current choice, for the popup's button.
    func selectionLabel(for facet: PresetFacet) -> String {
        switch choice(for: facet) {
        case .any:           return "Any"
        case .none:          return "None"
        case .value(let v):  return v
        }
    }

    // MARK: - Options and counts

    /// The non-Any choices a facet offers across the whole catalogue, value
    /// choices first then None (when the facet offers it and some preset is None).
    private func offeredChoices(for facet: PresetFacet, in ids: [String]) -> [FacetChoice] {
        var choices = allValues(for: facet, in: ids).sorted().map { FacetChoice.value($0) }
        if facet.offersNone, ids.contains(where: { facet.isNone(groupValues($0, facet.group)) }) {
            choices.append(.none)
        }
        return choices
    }

    /// The concrete values present for a facet across the given presets.
    private func allValues(for facet: PresetFacet, in ids: [String]) -> Set<String> {
        var values = Set<String>()
        for id in ids { values.formUnion(facet.concreteValues(from: groupValues(id, facet.group))) }
        return values
    }

    private func label(for choice: FacetChoice, facet: PresetFacet, count: Int) -> String {
        switch choice {
        case .any:          return "Any"
        case .none:         return "None (\(count))"
        case .value(let v): return "\(v) (\(count))"
        }
    }

    /// How many presets a choice would give: those it matches that also pass the
    /// search and the other facets' selections (this facet is excluded, since
    /// choosing within it replaces its current choice).
    private func count(for facet: PresetFacet, choice: FacetChoice, in presets: [MachinePreset]) -> Int {
        presets.filter { preset in
            matchesSearch(preset.presetId)
                && matchesSelection(preset.presetId, ignoring: facet)
                && matchesFacet(preset.presetId, facet: facet, choice: choice)
        }.count
    }

    // MARK: - Predicates

    private func groupValues(_ id: String, _ group: PresetTagGroup) -> Set<String> {
        Set((tagsById[id] ?? []).filter { $0.group == group }.map(\.value))
    }

    private func matchesSearch(_ id: String) -> Bool {
        let needle = searchText.trimmingCharacters(in: .whitespaces).lowercased()
        guard !needle.isEmpty else { return true }
        return (searchTextById[id] ?? "").contains(needle)
    }

    /// Whether a preset satisfies every facet's selection (AND), optionally
    /// ignoring one facet (for counting within that facet).
    private func matchesSelection(_ id: String, ignoring ignored: PresetFacet? = nil) -> Bool {
        for facet in PresetFacet.allCases where facet != ignored {
            if !matchesFacet(id, facet: facet, choice: choice(for: facet)) { return false }
        }
        return true
    }

    private func matchesFacet(_ id: String, facet: PresetFacet, choice: FacetChoice) -> Bool {
        let values = groupValues(id, facet.group)
        switch choice {
        case .any:          return true
        case .none:         return facet.isNone(values)
        case .value(let v): return values.contains(v)
        }
    }

    // MARK: - Bundle reading

    /// The preset's JSON object: `preset.json` from the `.beebium-preset` ZIP
    /// bundle, or the file read directly when it is plain JSON. nil when neither
    /// works, in which case only the row's model-derived tags remain. `nonisolated`
    /// so the indexer can read bundles off the main actor.
    nonisolated static func presetObject(atFilepath filepath: String) -> [String: Any]? {
        if let data = unzipEntry(named: "preset.json", from: filepath),
           let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any] {
            return object
        }
        if let data = try? Data(contentsOf: URL(fileURLWithPath: filepath)),
           let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any] {
            return object
        }
        return nil
    }

    /// Extract one entry's bytes from a ZIP with `/usr/bin/unzip -p`. The entries
    /// (a preset.json) are tiny, so reading to end before waiting cannot deadlock.
    nonisolated private static func unzipEntry(named entry: String, from filepath: String) -> Data? {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/unzip")
        process.arguments = ["-p", filepath, entry]
        let pipe = Pipe()
        process.standardOutput = pipe
        process.standardError = FileHandle.nullDevice
        do { try process.run() } catch { return nil }
        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        process.waitUntilExit()
        guard process.terminationStatus == 0, !data.isEmpty else { return nil }
        return data
    }
}
