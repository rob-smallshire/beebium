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

import Foundation

/// A dispatch intent for an EditableList, mapped to the proto EditableListEvent
/// in ExtensionUiClient.dispatch. Proto-agnostic so the selection and payload
/// logic stays testable without the generated types.
struct EditableListIntent: Equatable, Sendable {
    enum Kind: Sendable { case add, edit, remove, action }
    let kind: Kind
    let itemID: String      // empty for ADD
    let actionID: String    // set for ACTION only
    let commit: [EditorFieldCommit]  // set for ADD and EDIT
}

/// Selection and enablement logic for an EditableList, and construction of the
/// dispatch intents, kept out of the SwiftUI view so it is testable (#144). The
/// view owns one of these as @StateObject and feeds it the current rows on each
/// server push; it holds the selection across pushes.
@MainActor
final class EditableListViewModel: ObservableObject {

    /// The per-row facts the logic needs, adapted from the rendered
    /// EditableListItem. Kept minimal so the model does not depend on the proto.
    struct Row: Equatable, Identifiable {
        let id: String
        let removable: Bool
        let editable: Bool
    }

    @Published var selectedID: String?
    private(set) var rows: [Row]

    init(rows: [Row] = []) {
        self.rows = rows
    }

    /// Adopt a freshly pushed row set, dropping a selection whose row is gone so
    /// the "-" affordance never points at an item the server has removed.
    func update(rows: [Row]) {
        self.rows = rows
        if let selected = selectedID, !rows.contains(where: { $0.id == selected }) {
            selectedID = nil
        }
    }

    var selectedRow: Row? { rows.first { $0.id == selectedID } }

    /// "-" is enabled only when the selected row is removable.
    var canRemoveSelection: Bool { selectedRow?.removable == true }

    /// The selected row can be edited (double-click / Return / edit affordance).
    var canEditSelection: Bool { selectedRow?.editable == true }

    // MARK: - Dispatch intents (map 1:1 to EditableListEvent)

    /// REMOVE for the current selection, or nil when nothing removable is
    /// selected (so the caller never dispatches a no-op).
    func removeIntent() -> EditableListIntent? {
        guard let row = selectedRow, row.removable else { return nil }
        return EditableListIntent(kind: .remove, itemID: row.id, actionID: "", commit: [])
    }

    func actionIntent(itemID: String, actionID: String) -> EditableListIntent {
        EditableListIntent(kind: .action, itemID: itemID, actionID: actionID, commit: [])
    }

    func addIntent(commit: [EditorFieldCommit]) -> EditableListIntent {
        EditableListIntent(kind: .add, itemID: "", actionID: "", commit: commit)
    }

    func editIntent(itemID: String, commit: [EditorFieldCommit]) -> EditableListIntent {
        EditableListIntent(kind: .edit, itemID: itemID, actionID: "", commit: commit)
    }
}
