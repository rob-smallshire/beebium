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

/// Recursive SwiftUI renderer for the server-driven Extension UI tree.
///
/// One ExtensionViewRenderer is created per top-level View; the body
/// walks the proto Control tree and produces native widgets. Stable
/// `.id(control.id)` modifiers keep widget identity across pushes so
/// SwiftUI patches in place rather than rebuilding the panel each time.
///
/// The per-primitive subviews (`TextInputField`, `EditableChoiceField`,
/// `ComboBoxRepresentable`, `ModalEditorView`) live in their own files
/// alongside this one; this file holds only the recursive switch and
/// the in-line leaf renderers (Label / Indicator / Toggle / Button /
/// Choice / Group).
struct ExtensionViewRenderer: View {
    /// The control to render (typically a Group at the top of the tree).
    let control: Beebium_Control

    /// View revision the user is acting on. Stamped into outgoing
    /// Dispatch events so the server can reject events that reference
    /// a control the next push has already replaced.
    let viewRevision: UInt64

    /// Closure the renderer invokes when a widget produces an event.
    /// The framework / outer view layer is responsible for posting it
    /// over gRPC.
    let dispatch: (String, ExtensionDispatchPayload) -> Void

    /// Whether the server shares this host's filesystem. Gates the
    /// client-side "Reveal in Finder" on a FileReference; false (no
    /// reveal) is the safe default for a remote server. Propagates to
    /// nested controls through the recursive renderControl calls.
    var isServerLocal: Bool = false

    var body: some View {
        renderControl(control)
    }

    // Returns AnyView (type-erased) rather than `some View` because
    // this function is recursive (the .group branch calls back into
    // renderControl for each child). A recursive @ViewBuilder switch
    // returning seven different concrete View types triggers a
    // swift-frontend SIGILL deep inside ReplaceOpaqueTypesWithUnderlyingTypes
    // when whole-module optimisation is enabled (Release builds);
    // Debug builds with -Onone never substitute opaque types and do
    // not hit the bug. The cost of AnyView's type erasure is
    // negligible for a control tree of tens of nodes, and SwiftUI
    // still keeps stable widget identity via the .id(control.id)
    // modifier on each leaf.
    private func renderControl(_ control: Beebium_Control) -> AnyView {
        let body = renderControlBody(control)
        // Wire Control.tooltip into the platform-native hover-text
        // mechanism (SwiftUI .help()) when set. Empty / absent
        // tooltip leaves the modifier off entirely rather than
        // attaching an empty string, so accessibility tools don't
        // announce a spurious empty help string.
        if !control.tooltip.isEmpty {
            return AnyView(body.help(control.tooltip))
        }
        return body
    }

    private func renderControlBody(_ control: Beebium_Control) -> AnyView {
        switch control.control {
        case .label(let label):
            return AnyView(renderLabel(id: control.id, label: label))
        case .indicator(let indicator):
            return AnyView(renderIndicator(id: control.id, indicator: indicator))
        case .toggle(let toggle):
            return AnyView(renderToggle(id: control.id, toggle: toggle))
        case .button(let button):
            return AnyView(renderButton(id: control.id, button: button))
        case .choice(let choice):
            return AnyView(renderChoice(id: control.id, choice: choice))
        case .textInput(let textInput):
            return AnyView(TextInputField(controlId: control.id,
                                          textInput: textInput,
                                          dispatch: dispatch)
                .id(control.id))
        case .group(let group):
            return AnyView(renderGroup(id: control.id, group: group))
        case .modalEditor(let modal):
            return AnyView(ModalEditorView(controlId: control.id,
                                           modal: modal,
                                           dispatch: dispatch)
                .id(control.id))
        case .editableChoice(let ec):
            return AnyView(EditableChoiceField(controlId: control.id,
                                               editableChoice: ec,
                                               dispatch: dispatch)
                .id(control.id))
        case .editableList(let list):
            return AnyView(EditableListView(controlId: control.id,
                                            editableList: list,
                                            dispatch: dispatch)
                .id(control.id))
        case .fileReference(let fileReference):
            return AnyView(FileReferenceView(controlId: control.id,
                                             fileReference: fileReference,
                                             isServerLocal: isServerLocal,
                                             dispatch: dispatch)
                .id(control.id))
        case .none:
            return AnyView(EmptyView())
        }
    }

    // MARK: - Inline leaf renderers

    private func renderLabel(id: String, label: Beebium_Label) -> some View {
        // secondary_text (when present) is a renderer hint for short
        // provenance / status / size metadata attached to the primary
        // line. AppKit conventions push it right and de-emphasise it,
        // so the primary text reads as the focus and the secondary
        // disambiguates without competing for attention.
        HStack(spacing: 6) {
            Text(label.text)
                .frame(maxWidth: .infinity, alignment: .leading)
            if !label.secondaryText.isEmpty {
                Text(label.secondaryText)
                    .foregroundStyle(.secondary)
                    .font(.caption)
            }
        }
        .id(id)
    }

    private func renderIndicator(id: String, indicator: Beebium_Indicator) -> some View {
        HStack(spacing: 6) {
            Image(systemName: "circle.fill")
                .font(.system(size: 8))
                .foregroundColor(extensionUiIndicatorColor(indicator.state))
            Text(indicator.text)
                .foregroundColor(.secondary)
            Spacer()
        }
        .id(id)
    }

    private func renderToggle(id: String, toggle: Beebium_Toggle) -> some View {
        // Bind the toggle to a local source-of-truth derived from the
        // current pushed value. SwiftUI's Toggle wants a Binding; we
        // construct one whose setter dispatches and whose getter
        // returns whatever the server most recently said.
        let binding = Binding<Bool>(
            get: { toggle.value },
            set: { newValue in
                dispatch(id, .bool(newValue))
            }
        )
        return Toggle(toggle.label, isOn: binding)
            .toggleStyle(.switch)
            .id(id)
    }

    private func renderButton(id: String, button: Beebium_Button) -> some View {
        Button(button.label) {
            dispatch(id, .none)
        }
        .disabled(!button.enabled)
        .id(id)
    }

    private func renderChoice(id: String, choice: Beebium_Choice) -> some View {
        let binding = Binding<Int>(
            get: { Int(choice.selectedIndex) },
            set: { newValue in
                dispatch(id, .index(UInt32(newValue)))
            }
        )
        return Picker(choice.label, selection: binding) {
            ForEach(Array(choice.options.enumerated()), id: \.offset) { index, option in
                Text(option).tag(index)
            }
        }
        .id(id)
    }

    private func renderGroup(id: String, group: Beebium_Group) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            if !group.label.isEmpty {
                Text(group.label)
                    .font(.headline)
                    .foregroundColor(.secondary)
            }
            ForEach(Array(group.controls.enumerated()), id: \.element.id) { _, child in
                renderControl(child)
            }
        }
        .id(id)
    }
}

/// Module-internal so the per-primitive subview files (ModalEditorView's
/// anchor renderer, in particular) can share one implementation. The
/// frontend chooses how an Indicator state maps to colour for its
/// platform's accessibility conventions; macOS uses the system colour
/// roles directly.
func extensionUiIndicatorColor(_ state: Beebium_Indicator.State) -> Color {
    switch state {
    case .ok:      return .green
    case .warn:    return .yellow
    case .error:   return .red
    case .unknown: return .gray
    case .UNRECOGNIZED:
        return .gray
    }
}
