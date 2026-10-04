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

import SwiftUI

/// Renders an extension editor Control tree (a Group of fields) with a local
/// value buffer and Cancel / commit buttons. Shared by ModalEditorView and the
/// EditableList add/edit sheet (#144): both present the same prefilled editor and
/// bundle its buffered values into an EditorCommit only when the user commits, so
/// the server never sees an aborted edit. The buffers seed from the editor tree
/// when the form appears, matching what the server most recently published.
struct ExtensionEditorForm: View {

    let editor: Beebium_Control
    /// The commit button title: "Save" for an edit, "Add" for a new item.
    let commitTitle: String
    let showCancel: Bool
    let onCancel: () -> Void
    let onCommit: ([EditorFieldCommit]) -> Void

    /// Typed buffer slot for one editor sub-control's in-flight value.
    enum FieldBuffer: Sendable {
        case bool(Bool)
        case string(String)
        case index(UInt32)
    }

    @State private var buffers: [String: FieldBuffer] = [:]

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            editorBody(editor)
            HStack {
                if showCancel {
                    Button("Cancel", action: onCancel)
                        .keyboardShortcut(.cancelAction)
                }
                Spacer()
                Button(commitTitle) {
                    onCommit(buildFields())
                }
                .keyboardShortcut(.defaultAction)
            }
        }
        .activatesHostingWindow()
        .focusDiagnostics("extension-editor")
        .onAppear { buffers = collectInitialBuffers(editor) }
    }

    // MARK: - Editor tree

    // Returns AnyView for the same reason the main renderer does: the editor tree
    // can contain Groups whose children recurse here, and an opaque-type recursive
    // switch trips a swift-frontend compiler bug under Release whole-module
    // optimisation.
    private func editorBody(_ control: Beebium_Control) -> AnyView {
        switch control.control {
        case .group(let group):
            return AnyView(VStack(alignment: .leading, spacing: 8) {
                if !group.label.isEmpty {
                    Text(group.label)
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
                ForEach(Array(group.controls.enumerated()), id: \.element.id) { _, child in
                    editorBody(child)
                }
            }
            .id(control.id))
        case .textInput(let ti):
            let binding = Binding<String>(
                get: {
                    if case .string(let s) = buffers[control.id] { return s }
                    return ""
                },
                set: { buffers[control.id] = .string($0) }
            )
            return AnyView(VStack(alignment: .leading, spacing: 4) {
                ExtensionFieldLabel(text: ti.label, help: ti.help)
                // AppKit-backed field with a dedicated per-presentation editor,
                // so its caret shows on every open of the add/edit sheet, not
                // only the first (#153). Return commits the form, Escape cancels.
                PopoverTextField(text: binding,
                                 placeholder: ti.placeholder,
                                 diagnosticsLabel: "extension-editor",
                                 onSubmit: { onCommit(buildFields()) },
                                 onCancel: { if showCancel { onCancel() } })
                    .frame(height: 22)
                ExtensionFieldNote(note: ti.note)
            }
            .id(control.id))
        case .editableChoice(let ec):
            let binding = Binding<String>(
                get: {
                    if case .string(let s) = buffers[control.id] { return s }
                    return ""
                },
                set: { buffers[control.id] = .string($0) }
            )
            return AnyView(VStack(alignment: .leading, spacing: 4) {
                if !ec.label.isEmpty {
                    Text(ec.label).font(.caption).foregroundColor(.secondary)
                }
                ComboBoxRepresentable(value: binding,
                                      options: ec.options,
                                      placeholder: ec.placeholder,
                                      onCommit: { /* deferred to the form commit */ })
                    .frame(height: 24)
            }
            .id(control.id))
        case .choice(let ch):
            let binding = Binding<Int>(
                get: {
                    if case .index(let i) = buffers[control.id] { return Int(i) }
                    return 0
                },
                set: { buffers[control.id] = .index(UInt32($0)) }
            )
            return AnyView(Picker(ch.label, selection: binding) {
                ForEach(Array(ch.options.enumerated()), id: \.offset) { idx, option in
                    Text(option).tag(idx)
                }
            }
            .id(control.id))
        case .toggle(let tg):
            let binding = Binding<Bool>(
                get: {
                    if case .bool(let b) = buffers[control.id] { return b }
                    return false
                },
                set: { buffers[control.id] = .bool($0) }
            )
            return AnyView(Toggle(tg.label, isOn: binding)
                .toggleStyle(.switch)
                .id(control.id))
        case .label(let lbl):
            return AnyView(Text(lbl.text).id(control.id))
        default:
            // Nested ModalEditor / Button / Indicator inside an editor tree are
            // discouraged; skip rather than add speculative surface for shapes we
            // do not ship.
            return AnyView(EmptyView())
        }
    }

    // MARK: - Buffer plumbing

    private func collectInitialBuffers(_ control: Beebium_Control) -> [String: FieldBuffer] {
        var out: [String: FieldBuffer] = [:]
        populate(control, into: &out)
        return out
    }

    private func populate(_ control: Beebium_Control, into out: inout [String: FieldBuffer]) {
        switch control.control {
        case .group(let group):
            for child in group.controls {
                populate(child, into: &out)
            }
        case .textInput(let ti):
            out[control.id] = .string(ti.value)
        case .editableChoice(let ec):
            out[control.id] = .string(ec.value)
        case .choice(let ch):
            out[control.id] = .index(ch.selectedIndex)
        case .toggle(let tg):
            out[control.id] = .bool(tg.value)
        default:
            break
        }
    }

    private func buildFields() -> [EditorFieldCommit] {
        buffers.map { (id, value) in
            let fieldValue: EditorFieldCommit.EditorFieldValue
            switch value {
            case .bool(let b):   fieldValue = .bool(b)
            case .string(let s): fieldValue = .string(s)
            case .index(let i):  fieldValue = .index(i)
            }
            return EditorFieldCommit(fieldID: id, value: fieldValue)
        }
    }
}
