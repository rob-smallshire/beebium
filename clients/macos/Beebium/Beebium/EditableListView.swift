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

import AppKit
import SwiftUI

/// Renders a `Beebium_EditableList` (#144): a titled, bordered, compact list whose
/// rows show primary text left, secondary caption right, subtitle muted beneath,
/// an optional note, and the item state as a small indicator; beneath the list the
/// standard macOS gradient "+ -" segmented control, with "+" enabled when the list
/// allows adding and "-" only when a removable row is selected. Editing opens the
/// item's prefilled editor in a sheet (ADD uses the list's add_editor, Save titled
/// "Add"); per-item actions live in the row's context menu, each confirmed first
/// when it carries a warning. Labels are rendered verbatim and the renderer never
/// invents an action the view did not list. Selection and dispatch-intent logic
/// live in EditableListViewModel.
///
/// Used by `ExtensionViewRenderer` for the `.editableList` arm.
struct EditableListView: View {
    let controlId: String
    let editableList: Beebium_EditableList
    let dispatch: (String, ExtensionDispatchPayload) -> Void

    @StateObject private var model = EditableListViewModel()

    private enum EditSheet: Identifiable {
        case add
        case edit(String)                                    // item id
        case action(itemID: String, actionID: String)        // action with an editor
        var id: String {
            switch self {
            case .add:                            return "\u{1}add"
            case .edit(let id):                   return "\u{1}edit\u{1}" + id
            case .action(let itemID, let actionID):
                return "\u{1}action\u{1}" + itemID + "\u{1}" + actionID
            }
        }
    }
    @State private var sheet: EditSheet?

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            if !editableList.title.isEmpty || !editableList.help.isEmpty {
                HStack(spacing: 4) {
                    if !editableList.title.isEmpty {
                        Text(editableList.title)
                            .font(.caption)
                            .fontWeight(.semibold)
                            .foregroundColor(.secondary)
                            .textCase(.uppercase)
                    }
                    if !editableList.help.isEmpty {
                        ExtensionFieldHelpButton(help: editableList.help)
                    }
                }
            }
            listBox
            AddRemoveSegmentedControl(
                canAdd: editableList.canAdd,
                canRemove: model.canRemoveSelection,
                onAdd: { sheet = .add },
                onRemove: { removeSelection() })
                .fixedSize()
        }
        .onAppear { model.update(rows: Self.rows(from: editableList)) }
        .onChange(of: editableList) { newValue in
            model.update(rows: Self.rows(from: newValue))
        }
        .sheet(item: $sheet) { which in editorSheet(which) }
    }

    // MARK: - List

    private var listBox: some View {
        VStack(spacing: 0) {
            if editableList.items.isEmpty {
                HStack {
                    Text(emptyText)
                        .font(.caption)
                        .foregroundColor(.secondary)
                    Spacer(minLength: 0)
                }
                .padding(8)
            } else {
                ForEach(Array(editableList.items.enumerated()), id: \.element.id) { index, item in
                    rowView(item)
                    if index < editableList.items.count - 1 {
                        Divider()
                    }
                }
            }
        }
        .background(RoundedRectangle(cornerRadius: 6).fill(Color(nsColor: .textBackgroundColor)))
        .overlay(RoundedRectangle(cornerRadius: 6).stroke(Color(nsColor: .separatorColor)))
    }

    private var emptyText: String {
        editableList.emptyText.isEmpty ? "No items" : editableList.emptyText
    }

    private func rowView(_ item: Beebium_EditableListItem) -> some View {
        let selected = model.selectedID == item.id
        return HStack(alignment: .top, spacing: 6) {
            VStack(alignment: .leading, spacing: 2) {
                HStack {
                    Text(item.primary)
                        .font(.subheadline)
                        .lineLimit(1)
                        .truncationMode(.middle)
                    Spacer(minLength: 4)
                    if !item.secondary.isEmpty {
                        Text(item.secondary)
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                }
                if !item.subtitle.isEmpty {
                    Text(item.subtitle)
                        .font(.caption)
                        .foregroundColor(.secondary)
                        .lineLimit(1)
                        .truncationMode(.middle)
                }
                if !item.note.isEmpty {
                    HStack(spacing: 4) {
                        Image(systemName: "exclamationmark.triangle")
                            .foregroundColor(.orange)
                        Text(item.note)
                            .foregroundColor(.secondary)
                    }
                    .font(.caption)
                }
            }
            if item.state != .unknown {
                Image(systemName: "circle.fill")
                    .font(.system(size: 8))
                    .foregroundColor(extensionUiIndicatorColor(item.state))
                    .padding(.top, 3)
            }
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 5)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(selected ? Color.accentColor.opacity(0.18) : Color.clear)
        .contentShape(Rectangle())
        // Double-click edits an editable row; a single click selects it (so the
        // "-" control and Return act on it). The count:2 gesture is registered
        // first so SwiftUI prefers it over the single tap on a double click.
        .onTapGesture(count: 2) {
            model.selectedID = item.id
            if item.editable { sheet = .edit(item.id) }
        }
        .onTapGesture {
            model.selectedID = item.id
        }
        .contextMenu {
            ForEach(item.actions.indices, id: \.self) { actionIndex in
                let action = item.actions[actionIndex]
                Button(action.title) { runAction(action, itemID: item.id) }
            }
        }
    }

    // MARK: - Edit sheet

    private func editorSheet(_ which: EditSheet) -> some View {
        let editor: Beebium_Control
        let title: String
        let makeIntent: ([EditorFieldCommit]) -> EditableListIntent
        switch which {
        case .add:
            editor = editableList.addEditor
            title = "Add"
            makeIntent = { model.addIntent(commit: $0) }
        case .edit(let id):
            editor = editableList.items.first { $0.id == id }?.editor ?? Beebium_Control()
            title = "Save"
            makeIntent = { model.editIntent(itemID: id, commit: $0) }
        case .action(let itemID, let actionID):
            let action = editableList.items.first { $0.id == itemID }?
                .actions.first { $0.id == actionID }
            editor = action?.editor ?? Beebium_Control()
            // The commit button is titled for the action ("Save to map file"),
            // so the sheet reads as that action rather than a generic save.
            title = action?.title ?? "Save"
            makeIntent = { model.actionIntent(itemID: itemID, actionID: actionID, commit: $0) }
        }
        return VStack(alignment: .leading, spacing: 0) {
            ExtensionEditorForm(
                editor: editor,
                commitTitle: title,
                showCancel: true,
                onCancel: { sheet = nil },
                onCommit: { fields in
                    dispatch(controlId, .editableListEvent(makeIntent(fields)))
                    sheet = nil
                })
        }
        .padding()
        .frame(minWidth: 320)
    }

    // MARK: - Actions

    private func removeSelection() {
        guard let intent = model.removeIntent() else {
            NSLog("[EditableList] remove with no removable selection; ignoring")
            return
        }
        dispatch(controlId, .editableListEvent(intent))
    }

    /// Run a per-item action, confirming first when it carries a warning. The
    /// confirmation uses the AppKit sheet alert on the key window, dispatching the
    /// action only on the confirm button.
    private func runAction(_ action: Beebium_EditableListAction, itemID: String) {
        // An action that carries its own editor opens a prefilled sheet (the
        // sheet is the gate, so such an action does not also set a warning);
        // the ACTION dispatch then carries the edited values.
        if action.hasEditor {
            sheet = .action(itemID: itemID, actionID: action.id)
            return
        }
        if action.warning.isEmpty {
            dispatch(controlId, .editableListEvent(
                model.actionIntent(itemID: itemID, actionID: action.id)))
            return
        }
        guard let window = NSApp.keyWindow else {
            NSLog("[EditableList] no key window for the \"%@\" confirmation; not run",
                  action.title)
            return
        }
        let alert = NSAlert()
        alert.messageText = action.title
        alert.informativeText = action.warning
        alert.alertStyle = .warning
        alert.addButton(withTitle: action.title)
        alert.addButton(withTitle: "Cancel")
        alert.beginSheetModal(for: window) { response in
            Task { @MainActor in
                if response == .alertFirstButtonReturn {
                    dispatch(controlId, .editableListEvent(
                        model.actionIntent(itemID: itemID, actionID: action.id)))
                }
            }
        }
    }

    // MARK: - Adapters

    /// The view-model's minimal row facts, adapted from the rendered items.
    private static func rows(from list: Beebium_EditableList) -> [EditableListViewModel.Row] {
        list.items.map {
            EditableListViewModel.Row(id: $0.id, removable: $0.removable, editable: $0.editable)
        }
    }
}

/// The System Settings "+ -" add/remove control: a momentary, small-square
/// (gradient) NSSegmentedControl. "+" is enabled when the list can add and "-"
/// only when a removable row is selected; SwiftUI has no native equivalent with
/// per-segment enablement, so this wraps the AppKit control directly.
private struct AddRemoveSegmentedControl: NSViewRepresentable {
    let canAdd: Bool
    let canRemove: Bool
    let onAdd: () -> Void
    let onRemove: () -> Void

    func makeCoordinator() -> Coordinator {
        Coordinator(onAdd: onAdd, onRemove: onRemove)
    }

    func makeNSView(context: Context) -> NSSegmentedControl {
        let control = NSSegmentedControl()
        control.segmentCount = 2
        control.segmentStyle = .smallSquare
        control.controlSize = .small
        control.trackingMode = .momentary
        control.setImage(NSImage(systemSymbolName: "plus", accessibilityDescription: "Add"),
                         forSegment: 0)
        control.setImage(NSImage(systemSymbolName: "minus", accessibilityDescription: "Remove"),
                         forSegment: 1)
        control.target = context.coordinator
        control.action = #selector(Coordinator.fire(_:))
        return control
    }

    func updateNSView(_ control: NSSegmentedControl, context: Context) {
        context.coordinator.onAdd = onAdd
        context.coordinator.onRemove = onRemove
        control.setEnabled(canAdd, forSegment: 0)
        control.setEnabled(canRemove, forSegment: 1)
    }

    final class Coordinator: NSObject {
        var onAdd: () -> Void
        var onRemove: () -> Void

        init(onAdd: @escaping () -> Void, onRemove: @escaping () -> Void) {
            self.onAdd = onAdd
            self.onRemove = onRemove
        }

        @objc func fire(_ sender: NSSegmentedControl) {
            switch sender.selectedSegment {
            case 0: onAdd()
            case 1: onRemove()
            default: break
            }
        }
    }
}
