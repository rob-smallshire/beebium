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

import XCTest
@testable import Beebium

/// The preset catalogue search: a query matches a preset on its name, its
/// description and every facet value, case-insensitively, with each
/// space-separated word having to match somewhere (AND). "b+" is literal.
@MainActor
final class PresetFilterModelTests: XCTestCase {

    private func preset(_ id: String, name: String, description: String? = nil,
                        facets: [String: [String]] = [:]) -> MachinePreset {
        MachinePreset(id: UUID(), presetId: id, name: name, coreExecutablePath: "",
                      presetFilepath: "", facets: facets, source: .systemPreset,
                      machineDescription: nil, modelName: "", modelDescription: description,
                      releaseDate: nil, hasThumbnail: false)
    }

    /// A small catalogue mirroring real rows (name + description + facets).
    private func seeded() -> (PresetFilterModel, [MachinePreset]) {
        let presets = [
            preset("master", name: "Master 128", description: "BBC Master 128, MOS 3.20",
                   facets: ["model": ["Master 128"], "coprocessor": ["None"],
                            "disc_controller": ["1770"], "storage": ["Floppy", "Cassette"]]),
            preset("master-copro", name: "Master 128, internal 65C102 Copro (4 MHz)",
                   description: "Master 128, 65C102 copro",
                   facets: ["model": ["Master 128"], "coprocessor": ["65C102"],
                            "disc_controller": ["1770"], "storage": ["Floppy", "Cassette"]]),
            preset("bplus-music", name: "B+ 128K, Music 5000", description: "128K Model B+, Music 5000",
                   facets: ["model": ["Model B+"], "peripherals": ["Music 5000"],
                            "disc_controller": ["1770"], "storage": ["Floppy", "Cassette"]]),
            preset("aun-80", name: "Station 80 (AUN, Model B)", description: "Econet over AUN, ANFS + DFS",
                   facets: ["model": ["Model B"], "network": ["AUN"],
                            "disc_controller": ["1770"], "storage": ["Floppy", "Cassette"]]),
            // A preset whose 8271 is only in the facets, not the name/description,
            // so a match on it proves facet values are searched.
            preset("plain", name: "My Machine", description: "a custom build",
                   facets: ["model": ["Model B"], "disc_controller": ["8271"],
                            "storage": ["Floppy", "Cassette"]]),
        ]
        let model = PresetFilterModel()
        model.index(presets)
        return (model, presets)
    }

    private func results(_ model: PresetFilterModel, _ presets: [MachinePreset],
                         _ query: String) -> [String] {
        model.searchText = query
        return model.filtered(presets).map(\.presetId)
    }

    func testEmptyQueryReturnsEverythingInOrder() {
        let (model, presets) = seeded()
        XCTAssertEqual(results(model, presets, ""), presets.map(\.presetId))
        XCTAssertEqual(results(model, presets, "   "), presets.map(\.presetId))
    }

    func testMultiWordQueryAndsAcrossWords() {
        let (model, presets) = seeded()
        // "master" matches both Masters; "65c102" only the copro one -> their AND.
        XCTAssertEqual(results(model, presets, "65c102 master"), ["master-copro"])
    }

    func testCaseInsensitive() {
        let (model, presets) = seeded()
        XCTAssertEqual(Set(results(model, presets, "MASTER")), ["master", "master-copro"])
        XCTAssertEqual(results(model, presets, "aun 1770"), ["aun-80"])
    }

    func testBPlusIsMatchedLiterally() {
        let (model, presets) = seeded()
        // "b+" must not be tokenised to "b" (which would match everything); it
        // matches the B+ preset's name literally.
        XCTAssertEqual(results(model, presets, "b+"), ["bplus-music"])
        XCTAssertEqual(results(model, presets, "b+ music"), ["bplus-music"])
    }

    func testSearchesFacetValuesNotJustNameAndDescription() {
        let (model, presets) = seeded()
        // "8271" appears only in the plain preset's facets, nowhere in its text.
        XCTAssertEqual(results(model, presets, "8271"), ["plain"])
        // "floppy" is a facet value on several and in none of the names.
        XCTAssertEqual(Set(results(model, presets, "floppy")),
                       ["master", "master-copro", "bplus-music", "aun-80", "plain"])
    }

    func testNoMatchIsEmpty() {
        let (model, presets) = seeded()
        XCTAssertEqual(results(model, presets, "nonesuch"), [])
    }

    func testQueryWordsKeepPunctuation() {
        XCTAssertEqual(PresetFilterModel.queryWords(" B+  Music "), ["b+", "music"])
        XCTAssertEqual(PresetFilterModel.queryWords(""), [])
    }
}
