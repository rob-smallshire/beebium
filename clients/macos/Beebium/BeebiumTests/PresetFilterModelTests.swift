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
    /// 65C102 and no disc, and a B+ with a 65C02 and AUN on 1770. No boards and no
    /// add-ons at all.
    private func seeded() -> (PresetFilterModel, [MachinePreset]) {
        let model = PresetFilterModel()
        model.loadTagsForTesting([
            "p1": [t(.family, "Model B"), t(.disc, "1770")],
            "p2": [t(.family, "Model B"), t(.disc, "No disc")],
            "p3": [t(.family, "Master 128"), t(.coprocessor, "65C102"), t(.disc, "No disc")],
            "p4": [t(.family, "Model B+"), t(.coprocessor, "65C02"),
                   t(.econet, "AUN"), t(.disc, "1770")],
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
        let machine = popup(model, presets, .machine)
        XCTAssertEqual(count(machine, .value("Model B")), 2)
        XCTAssertEqual(count(machine, .value("Model B+")), 1)
        XCTAssertEqual(count(machine, .value("Master 128")), 1)
        // Machine has no None (every preset has a family).
        XCTAssertNil(count(machine, .none))
    }

    func testDiscNoneIsTheNoDiscValue() {
        let (model, presets) = seeded()
        let disc = popup(model, presets, .disc)
        // "No disc" is presented as None, not as its own value.
        XCTAssertEqual(count(disc, .value("1770")), 2)
        XCTAssertNil(count(disc, .value("No disc")))
        XCTAssertEqual(count(disc, .none), 2)   // p2, p3
    }

    func testCoprocessorNoneCountsPresetsWithoutOne() {
        let (model, presets) = seeded()
        let copro = popup(model, presets, .coprocessor)
        XCTAssertEqual(count(copro, .value("65C02")), 1)
        XCTAssertEqual(count(copro, .value("65C102")), 1)
        XCTAssertEqual(count(copro, .none), 2)   // p1, p2
    }

    func testFacetWithNothingToChooseIsHidden() {
        let (model, presets) = seeded()
        // No preset has a board or an add-on, so those popups never appear.
        XCTAssertNil(popup(model, presets, .board))
        XCTAssertNil(popup(model, presets, .addOns))
    }

    func testSelectionsAndTogether() {
        let (model, presets) = seeded()
        model.select(.machine, .value("Model B"))
        XCTAssertEqual(Set(model.filtered(presets).map(\.presetId)), ["p1", "p2"])
        model.select(.disc, .value("1770"))
        XCTAssertEqual(model.filtered(presets).map(\.presetId), ["p1"])
    }

    func testNoneChoiceFiltersAbsence() {
        let (model, presets) = seeded()
        model.select(.coprocessor, .none)
        XCTAssertEqual(Set(model.filtered(presets).map(\.presetId)), ["p1", "p2"])
        model.select(.coprocessor, .any)
        model.select(.disc, .none)
        XCTAssertEqual(Set(model.filtered(presets).map(\.presetId)), ["p2", "p3"])
    }

    func testCountsAreFacetedAgainstOtherSelections() {
        let (model, presets) = seeded()
        model.select(.machine, .value("Model B"))
        // Under Machine = Model B, the Disc popup counts only p1/p2.
        let disc = popup(model, presets, .disc)
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
        model.select(.machine, .value("Master 128"))
        XCTAssertTrue(model.isFiltering)
        model.reset()
        XCTAssertFalse(model.isFiltering)
        XCTAssertEqual(model.filtered(presets).count, 4)
    }
}
