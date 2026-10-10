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

import SwiftUI

/// Renders a structured `is_list` parameter (one with `fields`) through the very
/// same EditableList stack the extension sidebar uses, so adding a peer in the
/// Configurator reads exactly like adding one in the Network sidebar. It does
/// not fork those views: it adapts the draft to them.
///
/// The adapter is pure data in both directions. Outward, it builds a
/// `Beebium_EditableList` -- the proto the sidebar's server would otherwise push
/// -- from the parameter's fields, the draft's items and the resolve-preset
/// diagnostics. Inward, each `EditableListIntent` the view dispatches becomes a
/// new items array written straight back to the draft (there is no server to
/// round-trip through). The complexity avoided is a second list UI and a second
/// add/edit editor: EditableListView, EditableListViewModel, ExtensionEditorForm
/// and ExtensionFieldHelp stay the single implementations, driven here by a
/// schema field list instead of a runtime Control tree.
struct StructuredListParameterView: View {
    let parameter: ExtensionParameter
    /// The parameter's current items (objects or legacy strings) from the draft.
    let items: [JSONValue]
    /// Diagnostics for the enclosing instance; the ones under this parameter's
    /// items route to their row and field.
    var diagnostics: [PresetManager.PresetDiagnostic] = []
    /// Write (or, with nil, clear) this parameter's value in the draft.
    let setValue: (String, JSONValue?) -> Void

    var body: some View {
        EditableListView(
            controlId: parameter.key,
            editableList: Self.editableList(parameter: parameter, items: items,
                                            diagnostics: diagnostics),
            dispatch: dispatch)
    }

    private func dispatch(_ controlId: String, _ payload: ExtensionDispatchPayload) {
        guard case .editableListEvent(let intent) = payload else { return }
        let next = Self.apply(intent, to: items, fields: parameter.fields)
        setValue(parameter.key, next.isEmpty ? nil : .array(next))
    }

    // MARK: - Outward: draft -> Beebium_EditableList (pure, tested)

    /// The EditableList the views render, assembled from the parameter and the
    /// draft. The title carries the item count as the sidebar's does; the list
    /// always allows Add, and every row is editable and removable (the client
    /// owns the draft, so there is nothing the server must gate).
    static func editableList(parameter: ExtensionParameter, items: [JSONValue],
                             diagnostics: [PresetManager.PresetDiagnostic]) -> Beebium_EditableList {
        let template = ItemFormatTemplate(parameter.itemFormat)
        var list = Beebium_EditableList()
        let label = parameter.displayLabel
        list.title = items.isEmpty ? label : "\(label) (\(items.count))"
        list.help = parameter.description
        list.emptyText = "No \(label.lowercased())"
        list.canAdd = true
        list.addEditor = editor(fields: parameter.fields, strings: [:], fieldNotes: [:])
        list.items = items.enumerated().map { index, element in
            row(index: index, element: element, parameter: parameter,
                template: template, diagnostics: diagnostics)
        }
        return list
    }

    private static func row(index: Int, element: JSONValue, parameter: ExtensionParameter,
                            template: ItemFormatTemplate,
                            diagnostics: [PresetManager.PresetDiagnostic]) -> Beebium_EditableListItem {
        let strings: [String: String]
        let primary: String
        switch element {
        case .object(let object):
            strings = StructuredListItem.fieldStrings(fromObject: object, fields: parameter.fields)
            primary = StructuredListItem.render(strings: strings, template: template,
                                                fields: parameter.fields)
        case .string(let string):
            // A legacy string is already its own display line; parse it only to
            // prefill the editor, after which an edit writes it back as an object.
            strings = StructuredListItem.parse(string: string, template: template,
                                               fields: parameter.fields)
            primary = string
        default:
            strings = [:]
            primary = StructuredListItem.displayString(element)
        }

        var item = Beebium_EditableListItem()
        item.id = String(index)
        item.primary = primary
        item.editable = true
        item.removable = true

        // A diagnostic on this item marks the row (locating the problem) and,
        // when it names a field, prefills that field's note inside the editor.
        let rowDiagnostics = diagnostics.forListItem(key: parameter.key, index: index)
        if let worst = rowDiagnostics.first(where: \.isError) ?? rowDiagnostics.first {
            item.note = worst.message
            item.state = .warn
        }
        var fieldNotes: [String: String] = [:]
        for field in parameter.fields {
            let fieldDiagnostics = diagnostics.forListItemField(
                key: parameter.key, index: index, field: field.key)
            if let worst = fieldDiagnostics.first(where: \.isError) ?? fieldDiagnostics.first {
                fieldNotes[field.key] = worst.message
            }
        }
        item.editor = editor(fields: parameter.fields, strings: strings, fieldNotes: fieldNotes)
        return item
    }

    /// The editor Control tree for an item: a Group of one leaf per field. Add
    /// passes empty strings (so each field shows its default as a placeholder);
    /// edit passes the item's current field strings.
    static func editor(fields: [ExtensionListField], strings: [String: String],
                       fieldNotes: [String: String]) -> Beebium_Control {
        var group = Beebium_Group()
        group.controls = fields.map {
            fieldControl(for: $0, value: strings[$0.key] ?? "", note: fieldNotes[$0.key] ?? "")
        }
        var wrapper = Beebium_Control()
        wrapper.id = "item"
        wrapper.group = group
        return wrapper
    }

    private static func fieldControl(for field: ExtensionListField, value: String,
                                     note: String) -> Beebium_Control {
        var control = Beebium_Control()
        control.id = field.key
        if !field.choices.isEmpty {
            var choice = Beebium_Choice()
            choice.label = field.displayLabel
            choice.options = field.choices.map { $0.label.isEmpty ? $0.value : $0.label }
            choice.selectedIndex = UInt32(field.choices.firstIndex { $0.value == value } ?? 0)
            control.choice = choice
        } else if field.type == "boolean" {
            var toggle = Beebium_Toggle()
            toggle.label = field.displayLabel
            toggle.value = (value == "true" || value == "1")
            control.toggle = toggle
        } else {
            var input = Beebium_TextInput()
            input.label = field.displayLabel
            input.value = value
            input.placeholder = field.defaultValue
            input.help = field.description
            input.note = note
            control.textInput = input
        }
        return control
    }

    // MARK: - Inward: EditableListIntent -> new items (pure, tested)

    /// Apply one list event to the items array. Add appends the committed item,
    /// edit replaces the item at its id (the row index), remove drops it; an
    /// action is a no-op here (the Configurator defines no per-item actions).
    static func apply(_ intent: EditableListIntent, to items: [JSONValue],
                      fields: [ExtensionListField]) -> [JSONValue] {
        var result = items
        switch intent.kind {
        case .add:
            result.append(committedItem(intent.commit, fields: fields))
        case .edit:
            if let index = Int(intent.itemID), result.indices.contains(index) {
                result[index] = committedItem(intent.commit, fields: fields)
            }
        case .remove:
            if let index = Int(intent.itemID), result.indices.contains(index) {
                result.remove(at: index)
            }
        case .action:
            break
        }
        return result
    }

    private static func committedItem(_ commit: [EditorFieldCommit],
                                      fields: [ExtensionListField]) -> JSONValue {
        .object(StructuredListItem.object(fromFieldStrings: commitStrings(commit, fields: fields),
                                          fields: fields))
    }

    /// The committed field values as strings, keyed by field key. A choice's
    /// index is resolved back to its value.
    static func commitStrings(_ commit: [EditorFieldCommit],
                              fields: [ExtensionListField]) -> [String: String] {
        var out: [String: String] = [:]
        for field in commit {
            switch field.value {
            case .string(let s): out[field.fieldID] = s
            case .bool(let b):   out[field.fieldID] = b ? "true" : "false"
            case .index(let i):
                if let f = fields.first(where: { $0.key == field.fieldID }),
                   Int(i) < f.choices.count {
                    out[field.fieldID] = f.choices[Int(i)].value
                } else {
                    out[field.fieldID] = String(i)
                }
            }
        }
        return out
    }
}
