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
/// faceted counts, the None choice (absence, or "No disc" for Disc), and the
/// rule that a facet with nothing to choose is hidden. Tags are seeded directly,
/// so no files are read.
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

    /// A small catalogue: two Model B (one 1770, one no disc), a Master with a
    /// 65C102 and no disc, and a B+ with a 65C02 and AUN on 1770. No peripherals
    /// at all.
    private func seeded() -> (PresetFilterModel, [MachinePreset]) {
        let model = PresetFilterModel()
        model.loadTagsForTesting([
            "p1": [t(.model, "Model B"), t(.storage, "1770")],
            "p2": [t(.model, "Model B"), t(.storage, "No disc")],
            "p3": [t(.model, "Master 128"), t(.processor, "65C102"), t(.storage, "No disc")],
            "p4": [t(.model, "Model B+"), t(.processor, "65C02"),
                   t(.network, "AUN"), t(.storage, "1770")],
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

    func testPopupsOfferTheirValuesWithCounts() {
        let (model, presets) = seeded()
        let modelFacet = popup(model, presets, .model)
        XCTAssertEqual(count(modelFacet, .value("Model B")), 2)
        XCTAssertEqual(count(modelFacet, .value("Model B+")), 1)
        XCTAssertEqual(count(modelFacet, .value("Master 128")), 1)
        // Model has no None (every preset has a family).
        XCTAssertNil(count(modelFacet, .none))
    }

    func testStorageNoneIsTheNoDiscValue() {
        let (model, presets) = seeded()
        let storage = popup(model, presets, .storage)
        // "No disc" is presented as None, not as its own value.
        XCTAssertEqual(count(storage, .value("1770")), 2)
        XCTAssertNil(count(storage, .value("No disc")))
        XCTAssertEqual(count(storage, .none), 2)   // p2, p3
    }

    func testProcessorNoneCountsPresetsWithoutOne() {
        let (model, presets) = seeded()
        let processor = popup(model, presets, .processor)
        XCTAssertEqual(count(processor, .value("65C02")), 1)
        XCTAssertEqual(count(processor, .value("65C102")), 1)
        XCTAssertEqual(count(processor, .none), 2)   // p1, p2
    }

    func testFacetWithNothingToChooseIsHidden() {
        let (model, presets) = seeded()
        // No preset has a peripheral, so that popup never appears.
        XCTAssertNil(popup(model, presets, .peripherals))
    }

    func testSelectionsAndTogether() {
        let (model, presets) = seeded()
        model.select(.model, .value("Model B"))
        XCTAssertEqual(Set(model.filtered(presets).map(\.presetId)), ["p1", "p2"])
        model.select(.storage, .value("1770"))
        XCTAssertEqual(model.filtered(presets).map(\.presetId), ["p1"])
    }

    func testNoneChoiceFiltersAbsence() {
        let (model, presets) = seeded()
        model.select(.processor, .none)
        XCTAssertEqual(Set(model.filtered(presets).map(\.presetId)), ["p1", "p2"])
        model.select(.processor, .any)
        model.select(.storage, .none)
        XCTAssertEqual(Set(model.filtered(presets).map(\.presetId)), ["p2", "p3"])
    }

    func testCountsAreFacetedAgainstOtherSelections() {
        let (model, presets) = seeded()
        model.select(.model, .value("Model B"))
        // Under Model = Model B, the Storage popup counts only p1/p2.
        let storage = popup(model, presets, .storage)
        XCTAssertEqual(count(storage, .value("1770")), 1)   // p1
        XCTAssertEqual(count(storage, .none), 1)            // p2
        // A choice that can give nothing is disabled (65C102 is Master-only).
        let processor = popup(model, presets, .processor)
        let c102 = processor?.options.first { $0.choice == .value("65C102") }
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
