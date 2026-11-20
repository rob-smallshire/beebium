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

/// Searches the preset catalogue (client-specification.md 5.1). The Presets page
/// shows one search field and nothing else: the query matches a preset on its
/// name, its description and every value of its facets (the server's index --
/// model, coprocessor, disc controller, network, storage values, peripherals
/// values), case-insensitively, each space-separated word having to match
/// somewhere. So "master copro", "b+ music" or "aun 1770" each narrow to the
/// presets a person means. The client derives nothing from preset content; it
/// indexes the rows the server already classified.
@MainActor
final class PresetFilterModel: ObservableObject {
    @Published var searchText: String = ""

    /// Per preset, the lower-cased blob searched: its name, description and every
    /// facet value joined. Published so the catalogue re-renders once indexed.
    @Published private var blobById: [String: String] = [:]

    /// Build the search blob for each preset from the row (its name, description
    /// and facet values). Cheap and idempotent; no files, no subprocesses.
    func index(_ presets: [MachinePreset]) {
        var blobs: [String: String] = [:]
        for preset in presets {
            blobs[preset.presetId] = Self.blob(for: preset)
        }
        blobById = blobs
    }

    /// The presets matching the current query. Empty query -> all presets, in
    /// their given order.
    func filtered(_ presets: [MachinePreset]) -> [MachinePreset] {
        let words = Self.queryWords(searchText)
        guard !words.isEmpty else { return presets }
        return presets.filter { preset in
            let blob = blobById[preset.presetId] ?? Self.blob(for: preset)
            return words.allSatisfy { blob.contains($0) }
        }
    }

    /// The lower-cased searchable text for a preset: its name, description and
    /// every facet value.
    static func blob(for preset: MachinePreset) -> String {
        var parts = [preset.name]
        if let description = preset.modelDescription { parts.append(description) }
        for values in preset.facets.values { parts.append(contentsOf: values) }
        return parts.joined(separator: " ").lowercased()
    }

    /// The query split into lower-cased words on whitespace. Punctuation is kept,
    /// so "b+" stays "b+" and matches "B+" literally rather than widening to "b".
    static func queryWords(_ query: String) -> [String] {
        query.lowercased().split(whereSeparator: { $0.isWhitespace }).map(String.init)
    }
}
