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

/// The preset filter's popup logic: single-selection facets that AND together,
/// faceted counts, the None choice (absence, or "No disc" for the exclusive Disc
/// controller facet), set facets where choosing a value means "has", and the rule
/// that a facet with nothing to choose is hidden. Tags are seeded directly, so no
/// files are read.
@MainActor
final class PresetFilterModelTests: XCTestCase {

    private func t(_ group: PresetTagGroup, _ value: String) -> PresetTag {
        PresetTag(group: group, value: value)
    }

    private func preset(_ id: String) -> MachinePreset {
        MachinePreset(id: UUID(), presetId: id, name: id, coreExecutablePath: "",
                      presetFilepath: "", source: .systemPreset, machineDescription: nil,
                      modelName: "", modelDescription: nil, releaseDate: nil, hasThumbnail: false)
    }

    /// A small catalogue exercising both facet kinds:
    /// - p1: Model B, 1770, Floppy.
    /// - p2: Model B, no disc, no storage.
    /// - p3: Master 128, 65C102, 1770, Floppy.
    /// - p4: Model B+, 65C02, AUN, 1770, Floppy + Hard disc, Music 5000.
    private func seeded() -> (PresetFilterModel, [MachinePreset]) {
        let model = PresetFilterModel()
        model.loadTagsForTesting([
            "p1": [t(.model, "Model B"), t(.discController, "1770"), t(.storage, "Floppy")],
            "p2": [t(.model, "Model B"), t(.discController, "No disc")],
            "p3": [t(.model, "Master 128"), t(.coprocessor, "65C102"),
                   t(.discController, "1770"), t(.storage, "Floppy")],
            "p4": [t(.model, "Model B+"), t(.coprocessor, "65C02"), t(.network, "AUN"),
                   t(.discController, "1770"), t(.storage, "Floppy"), t(.storage, "Hard disc"),
                   t(.peripherals, "Music 5000")],
        ])
        return (model, ["p1", "p2", "p3", "p4"].map(preset))
    }

    private func popup(_ model: PresetFilterModel, _ presets: [MachinePreset],
                       _ facet: PresetFacet) -> PresetFilterModel.Popup? {
        model.popups(for: presets).first { $0.facet == facet }
    }

    private func count(_ popup: PresetFilterModel.Popup?, _ choice: FacetChoice) -> Int? {
        popup?.options.first { $0.choice == choice }?.count
    }

    func testModelPopupOffersItsValuesWithCounts() {
        let (model, presets) = seeded()
        let modelFacet = popup(model, presets, .model)
        XCTAssertEqual(count(modelFacet, .value("Model B")), 2)
        XCTAssertEqual(count(modelFacet, .value("Model B+")), 1)
        XCTAssertEqual(count(modelFacet, .value("Master 128")), 1)
        // Model has no None (every preset has one).
        XCTAssertNil(count(modelFacet, .none))
    }

    func testDiscControllerNoneIsTheNoDiscValue() {
        let (model, presets) = seeded()
        let disc = popup(model, presets, .discController)
        XCTAssertEqual(count(disc, .value("1770")), 3)   // p1, p3, p4
        XCTAssertNil(count(disc, .value("No disc")))     // shown as None, not a value
        XCTAssertEqual(count(disc, .none), 1)            // p2
    }

    func testCoprocessorNoneCountsPresetsWithoutOne() {
        let (model, presets) = seeded()
        let copro = popup(model, presets, .coprocessor)
        XCTAssertEqual(count(copro, .value("65C02")), 1)
        XCTAssertEqual(count(copro, .value("65C102")), 1)
        XCTAssertEqual(count(copro, .none), 2)           // p1, p2
    }

    func testStorageIsASetFacetWhereValueMeansHas() {
        let (model, presets) = seeded()
        let storage = popup(model, presets, .storage)
        XCTAssertEqual(count(storage, .value("Floppy")), 3)     // p1, p3, p4
        XCTAssertEqual(count(storage, .value("Hard disc")), 1)  // p4
        XCTAssertEqual(count(storage, .none), 1)                // p2 has no storage
        // Choosing Hard disc keeps only the preset that has it.
        model.select(.storage, .value("Hard disc"))
        XCTAssertEqual(model.filtered(presets).map(\.presetId), ["p4"])
    }

    func testFacetWithNothingToChooseIsHidden() {
        // A catalogue where every preset has the one same model and no other
        // facet: only Model has (one) value, so no popup can be built.
        let model = PresetFilterModel()
        model.loadTagsForTesting([
            "a": [t(.model, "Model B")],
            "b": [t(.model, "Model B")],
        ])
        let presets = ["a", "b"].map(preset)
        // Model has a single value (no None) -> fewer than two choices -> hidden.
        XCTAssertNil(popup(model, presets, .model))
        // The other facets have no values at all -> also hidden.
        XCTAssertNil(popup(model, presets, .storage))
        XCTAssertNil(popup(model, presets, .network))
    }

    func testSelectionsAndTogether() {
        let (model, presets) = seeded()
        model.select(.model, .value("Model B"))
        XCTAssertEqual(Set(model.filtered(presets).map(\.presetId)), ["p1", "p2"])
        model.select(.discController, .value("1770"))
        XCTAssertEqual(model.filtered(presets).map(\.presetId), ["p1"])
    }

    func testNoneChoiceFiltersAbsence() {
        let (model, presets) = seeded()
        model.select(.coprocessor, .none)
        XCTAssertEqual(Set(model.filtered(presets).map(\.presetId)), ["p1", "p2"])
        model.select(.coprocessor, .any)
        model.select(.storage, .none)
        XCTAssertEqual(model.filtered(presets).map(\.presetId), ["p2"])   // only p2 has no storage
    }

    func testCountsAreFacetedAgainstOtherSelections() {
        let (model, presets) = seeded()
        model.select(.model, .value("Model B"))
        // Under Model = Model B, the Disc controller popup counts only p1/p2.
        let disc = popup(model, presets, .discController)
        XCTAssertEqual(count(disc, .value("1770")), 1)   // p1
        XCTAssertEqual(count(disc, .none), 1)            // p2
        // A choice that can give nothing is disabled (65C102 is Master-only).
        let copro = popup(model, presets, .coprocessor)
        let c102 = copro?.options.first { $0.choice == .value("65C102") }
        XCTAssertEqual(c102?.count, 0)
        XCTAssertEqual(c102?.isEnabled, false)
    }

    func testResetClearsEverySelection() {
        let (model, presets) = seeded()
        model.select(.model, .value("Master 128"))
        XCTAssertTrue(model.isFiltering)
        model.reset()
        XCTAssertFalse(model.isFiltering)
        XCTAssertEqual(model.filtered(presets).count, 4)
    }
}
