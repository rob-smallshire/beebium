// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
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

/// Selection/enablement and dispatch-intent logic for EditableList (#144): the
/// "-" and edit affordances follow the selected row's flags, a stale selection is
/// dropped on a server push, and the four intents carry the right fields.
@MainActor
final class EditableListViewModelTests: XCTestCase {

    private func row(_ id: String, removable: Bool = false, editable: Bool = false)
        -> EditableListViewModel.Row {
        EditableListViewModel.Row(id: id, removable: removable, editable: editable)
    }

    func testNoSelectionDisablesRemoveAndEdit() {
        let model = EditableListViewModel(rows: [row("a", removable: true, editable: true)])
        XCTAssertNil(model.selectedRow)
        XCTAssertFalse(model.canRemoveSelection)
        XCTAssertFalse(model.canEditSelection)
        XCTAssertNil(model.removeIntent())
    }

    func testRemoveEnabledOnlyForRemovableSelection() {
        let model = EditableListViewModel(rows: [row("a", removable: false),
                                                 row("b", removable: true)])
        model.selectedID = "a"
        XCTAssertFalse(model.canRemoveSelection)
        XCTAssertNil(model.removeIntent())

        model.selectedID = "b"
        XCTAssertTrue(model.canRemoveSelection)
        XCTAssertEqual(model.removeIntent(),
                       EditableListIntent(kind: .remove, itemID: "b", actionID: "", commit: []))
    }

    func testEditEnabledOnlyForEditableSelection() {
        let model = EditableListViewModel(rows: [row("a", editable: false),
                                                 row("b", editable: true)])
        model.selectedID = "a"
        XCTAssertFalse(model.canEditSelection)
        model.selectedID = "b"
        XCTAssertTrue(model.canEditSelection)
    }

    func testUpdateDropsStaleSelection() {
        let model = EditableListViewModel(rows: [row("a", removable: true)])
        model.selectedID = "a"
        model.update(rows: [row("b", removable: true)])   // the selected "a" is gone
        XCTAssertNil(model.selectedID)
        XCTAssertFalse(model.canRemoveSelection)
    }

    func testUpdateKeepsLiveSelection() {
        let model = EditableListViewModel(rows: [row("a", removable: true)])
        model.selectedID = "a"
        model.update(rows: [row("a", removable: true), row("b")])
        XCTAssertEqual(model.selectedID, "a")
    }

    func testAddEditActionIntentsCarryTheirFields() {
        let model = EditableListViewModel(rows: [row("x", editable: true)])
        let fields = [EditorFieldCommit(fieldID: "f", value: .string("v"))]
        XCTAssertEqual(model.addIntent(commit: fields),
                       EditableListIntent(kind: .add, itemID: "", actionID: "", commit: fields))
        XCTAssertEqual(model.editIntent(itemID: "x", commit: fields),
                       EditableListIntent(kind: .edit, itemID: "x", actionID: "", commit: fields))
        XCTAssertEqual(model.actionIntent(itemID: "x", actionID: "save"),
                       EditableListIntent(kind: .action, itemID: "x", actionID: "save", commit: []))
    }
}
