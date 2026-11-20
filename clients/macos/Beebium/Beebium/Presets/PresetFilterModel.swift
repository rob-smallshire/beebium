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

/// Spike state behind the preset catalogue's filter chips and search field:
/// which tags each preset carries, which chips are on, and the search text. It
/// reads each preset file once (local, for the spike) to derive its tags via
/// `PresetTagger`, then filters the catalogue. No persistence -- the state lives
/// only as long as the window.
///
/// Chip semantics: within a group the selected chips OR together; across groups
/// they AND together. A chip's count is faceted -- how many presets it would
/// match given the other groups' current selection and the search -- so a chip
/// that can add nothing reads zero and greys out.
@MainActor
final class PresetFilterModel: ObservableObject {
    @Published var searchText: String = ""
    @Published private(set) var selected: Set<PresetTag> = []

    /// Tags per preset id, built by `index`.
    private var tagsById: [String: Set<PresetTag>] = [:]
    /// Lower-cased "name description" per preset id, for substring search.
    private var searchTextById: [String: String] = [:]

    /// A chip in the bar: a tag with its faceted count and whether it is on.
    struct Chip: Identifiable {
        let tag: PresetTag
        let count: Int
        let isSelected: Bool
        var id: PresetTag { tag }
        /// A chip that can match nothing (and is not already on) greys out.
        var isEnabled: Bool { isSelected || count > 0 }
    }

    /// One group's chips, in value order.
    struct ChipGroup: Identifiable {
        let group: PresetTagGroup
        let chips: [Chip]
        var id: PresetTagGroup { group }
    }

    var isFiltering: Bool { !selected.isEmpty || !searchText.isEmpty }

    // MARK: - Indexing

    /// Derive and cache every preset's tags and searchable text. Reads the preset
    /// files directly, which is fine for a local spike; the server will supply the
    /// tags later. Re-indexing the same list is cheap and idempotent.
    func index(_ presets: [MachinePreset]) {
        var tags: [String: Set<PresetTag>] = [:]
        var search: [String: String] = [:]
        for preset in presets {
            tags[preset.presetId] = Set(Self.tags(for: preset))
            let description = preset.modelDescription ?? ""
            search[preset.presetId] = "\(preset.name) \(description)".lowercased()
        }
        tagsById = tags
        searchTextById = search
        // Drop any selection no longer offered by the catalogue.
        let available = Set(tags.values.flatMap { $0 })
        selected.formIntersection(available)
    }

    private static func tags(for preset: MachinePreset) -> [PresetTag] {
        guard let data = try? Data(contentsOf: URL(fileURLWithPath: preset.presetFilepath)),
              let json = try? JSONSerialization.jsonObject(with: data),
              let object = json as? [String: Any]
        else { return [] }
        // The file carries its own `model`, so the tagger reads it from there.
        return PresetTagger.tags(model: nil, preset: object)
    }

    // MARK: - Selection

    func toggle(_ tag: PresetTag) {
        if selected.contains(tag) { selected.remove(tag) } else { selected.insert(tag) }
    }

    /// Clear the chip selection (not the search field, which clears itself).
    func clearChips() {
        selected.removeAll()
    }

    // MARK: - Filtering

    func filtered(_ presets: [MachinePreset]) -> [MachinePreset] {
        presets.filter { matchesSearch($0.presetId) && matchesChips($0.presetId) }
    }

    /// The chip bar for the catalogue: one group per tag group present, each with
    /// its values and their faceted counts, in group-then-value order.
    func chipGroups(for presets: [MachinePreset]) -> [ChipGroup] {
        let available = presets.compactMap { tagsById[$0.presetId] }.reduce(into: Set<PresetTag>()) {
            $0.formUnion($1)
        }
        let byGroup = Dictionary(grouping: available, by: \.group)
        return byGroup.keys.sorted().map { group in
            let chips = byGroup[group, default: []].sorted().map { tag in
                Chip(tag: tag, count: count(for: tag, in: presets), isSelected: selected.contains(tag))
            }
            return ChipGroup(group: group, chips: chips)
        }
    }

    // MARK: - Predicates

    private func matchesSearch(_ id: String) -> Bool {
        let needle = searchText.trimmingCharacters(in: .whitespaces).lowercased()
        guard !needle.isEmpty else { return true }
        return (searchTextById[id] ?? "").contains(needle)
    }

    /// Whether a preset satisfies the chip selection: within each selected group
    /// its tags must intersect (OR), and every selected group must be satisfied
    /// (AND). `ignoring` drops one group, for counting a chip within its own group.
    private func matchesChips(_ id: String, ignoring ignored: PresetTagGroup? = nil) -> Bool {
        let tags = tagsById[id] ?? []
        let selectedByGroup = Dictionary(grouping: selected, by: \.group)
        for (group, groupSelection) in selectedByGroup where group != ignored {
            if tags.isDisjoint(with: Set(groupSelection)) { return false }
        }
        return true
    }

    /// How many presets a chip would match: those carrying its tag that also pass
    /// the search and the other groups' selection (its own group is OR, so it is
    /// ignored here).
    private func count(for tag: PresetTag, in presets: [MachinePreset]) -> Int {
        presets.filter { preset in
            matchesSearch(preset.presetId)
                && matchesChips(preset.presetId, ignoring: tag.group)
                && (tagsById[preset.presetId]?.contains(tag) ?? false)
        }.count
    }
}
