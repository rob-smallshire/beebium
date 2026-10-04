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

import AppKit
import SwiftUI


/// The rename editor: the machine's name *template*, a picker of the server's
/// placeholders, and a live preview of the rendered name (#153).
///
/// The field shows the template, not the rendered name, because the template is
/// what the user is changing. The picker, its groups, labels, values and the
/// `{key}` insertion text all come from the server, so the app carries no list
/// of placeholder keys. If the server is too old to offer the placeholder and
/// preview RPCs, this degrades to a plain name field with neither picker nor
/// preview.
// Not private so a hosted test can open the real editor and observe its field.
struct MachineRenameEditor: View {
    @ObservedObject var systemClient: SystemClient
    let dismiss: () -> Void

    @State private var draft: String
    @State private var originalTemplate: String
    @State private var placeholders: [Beebium_NamePlaceholder] = []
    @State private var pickerAvailable = false
    @State private var previewAvailable = false
    @State private var preview: String = ""
    @State private var note: String = ""
    @State private var didLoad = false
    @State private var isSaving = false
    @State private var saveError: String?

    @StateObject private var fieldController = TemplateFieldController()
    @State private var previewDebouncer = Debouncer(delay: 0.25)

    init(systemClient: SystemClient, dismiss: @escaping () -> Void) {
        self._systemClient = ObservedObject(wrappedValue: systemClient)
        self.dismiss = dismiss
        // Seed the field BEFORE it is created so it opens showing the template
        // rather than empty. An empty template means a server too old to carry
        // one; fall back to the rendered name so the plain field is not blank.
        let template = systemClient.machineNameTemplate.isEmpty
            ? systemClient.machineName
            : systemClient.machineNameTemplate
        self._draft = State(initialValue: template)
        self._originalTemplate = State(initialValue: template)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            PopoverTextField(text: $draft,
                             placeholder: "Name template",
                             controller: fieldController,
                             diagnosticsLabel: "rename",
                             onSubmit: { save() },
                             onCancel: { dismiss() })
                .frame(maxWidth: .infinity)
                .frame(height: 22)

            if pickerAvailable {
                placeholderPicker
            }

            if previewAvailable {
                Divider()
                previewSection
            }

            if let saveError {
                Text(saveError)
                    .font(.caption)
                    .foregroundColor(.red)
                    .fixedSize(horizontal: false, vertical: true)
            }

            // Explicit, visible commit, matching the Econet station editor. The
            // only way to commit is Save (or Return); Cancel, Escape and clicking
            // outside the transient popover all discard.
            HStack {
                Button("Cancel") { dismiss() }
                    .keyboardShortcut(.cancelAction)
                Spacer()
                Button("Save") { save() }
                    .keyboardShortcut(.defaultAction)
                    .disabled(isSaving || !isDirty)
            }
        }
        .padding(12)
        .frame(width: 440)
        .activatesHostingWindow()
        .focusDiagnostics("rename")
        .onAppear(perform: load)
        .onDisappear { previewDebouncer.cancel() }
        .onChange(of: draft) { _ in
            saveError = nil
            previewDebouncer.schedule {
                Task { await refreshPreview() }
            }
        }
    }

    /// Save is offered only for a non-empty template that differs from the one
    /// the machine already has: an empty template is refused by the server and an
    /// unchanged one is a wasted round trip.
    private var isDirty: Bool {
        let trimmed = draft.trimmingCharacters(in: .whitespacesAndNewlines)
        return !trimmed.isEmpty && trimmed != originalTemplate
    }

    // MARK: - Picker

    private struct PlaceholderGroup: Identifiable {
        let id: String
        let items: [Beebium_NamePlaceholder]
    }

    /// Placeholders grouped by their `group`, keeping the server's order both of
    /// groups (first appearance) and of items within a group.
    private var groupedPlaceholders: [PlaceholderGroup] {
        var order: [String] = []
        var byGroup: [String: [Beebium_NamePlaceholder]] = [:]
        for placeholder in placeholders {
            if byGroup[placeholder.group] == nil { order.append(placeholder.group) }
            byGroup[placeholder.group, default: []].append(placeholder)
        }
        return order.map { PlaceholderGroup(id: $0, items: byGroup[$0] ?? []) }
    }

    private var placeholderPicker: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Insert a field at the caret")
                .font(.caption)
                .foregroundColor(.secondary)
            ForEach(groupedPlaceholders) { group in
                VStack(alignment: .leading, spacing: 4) {
                    Text(group.id)
                        .font(.caption2)
                        .foregroundColor(.secondary)
                    ForEach(group.items, id: \.key) { item in
                        placeholderRow(item)
                    }
                }
            }
        }
    }

    // Each row is a button-like chip whose text is the template insertion string
    // ("{econet-station}"), so the row shows exactly what it inserts and reads as
    // clickable; the current value sits beside it and the human label and
    // description are the tooltip. A placeholder with no value on this machine
    // (an Econet field with no Econet fitted) is dimmed, not hidden: it is still
    // a valid template key and may gain a value later, so it stays insertable.
    private func placeholderRow(_ item: Beebium_NamePlaceholder) -> some View {
        HStack(spacing: 8) {
            Button {
                fieldController.insertAtCaret(item.insertion, text: $draft)
            } label: {
                Text(item.insertion)
                    .font(.caption.monospaced())
            }
            .buttonStyle(.bordered)
            .controlSize(.small)
            // Non-activating: a chip must not pull focus out of the template
            // field (the picker keeps the editing session). insertAtCaret also
            // re-asserts first responder afterwards as a belt-and-braces.
            .focusable(false)
            .help(NameTemplatePlaceholderTooltip.text(label: item.label,
                                                      description: item.description_p))
            Text(item.applicable ? item.value : "not applicable")
                .font(.caption)
                .foregroundColor(.secondary)
                .lineLimit(1)
                .truncationMode(.tail)
            Spacer(minLength: 0)
        }
        .opacity(item.applicable ? 1.0 : 0.5)
    }

    // MARK: - Preview

    private var previewSection: some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack(spacing: 4) {
                Text("Preview:")
                    .foregroundColor(.secondary)
                Text(preview.isEmpty ? "(empty)" : preview)
                    .lineLimit(1)
                    .truncationMode(.middle)
            }
            .font(.caption)
            if !note.isEmpty {
                Text(note)
                    .font(.caption2)
                    .foregroundColor(.orange)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
    }

    // MARK: - Behaviour

    private func load() {
        guard !didLoad else { return }
        didLoad = true
        // draft and originalTemplate are seeded in init so the field opens filled;
        // here we only fetch the server-driven picker and the initial preview.
        Task {
            if let list = await systemClient.listNamePlaceholders() {
                placeholders = list
                pickerAvailable = !list.isEmpty
            }
            await refreshPreview()
        }
    }

    private func refreshPreview() async {
        guard let result = await systemClient.previewMachineName(draft) else {
            previewAvailable = false
            return
        }
        previewAvailable = true
        preview = result.rendered
        note = NameTemplateNote.text(unknownKeys: result.report.unknownKeys,
                                     malformed: result.report.malformed)
    }

    /// Commit the template. On success the popover closes and the window title
    /// updates from the applied identity (and again from the status stream's
    /// IDENTITY_CHANGED). On failure the popover stays open and shows the reason,
    /// the way the station editor does.
    private func save() {
        guard isDirty, !isSaving else { return }
        let trimmed = draft.trimmingCharacters(in: .whitespacesAndNewlines)
        isSaving = true
        saveError = nil
        Task {
            let result = await systemClient.setMachineName(trimmed)
            isSaving = false
            switch result {
            case .success:
                dismiss()
            case .failure(let error):
                saveError = error.localizedDescription
            }
        }
    }
}

/// Shows the rename editor in a popover pointing at the window's title.
///
/// A popover rather than a sheet: a sheet takes the whole window hostage for a
/// single short string, and says nothing about which window's machine is being
/// renamed. A popover points at the name it is about to change.
@MainActor
final class MachineRenamePopover: NSObject, NSPopoverDelegate {
    private var popover: NSPopover?

    func show(in window: NSWindow, systemClient: SystemClient) {
        // A second request while one is open should not stack popovers; the one
        // already there is the one the user asked for.
        if popover?.isShown == true { return }

        guard let titleBar = window.standardWindowButton(.closeButton)?.superview else {
            return
        }

        let popover = NSPopover()
        popover.behavior = .transient
        popover.delegate = self
        popover.contentViewController = NSHostingController(
            rootView: MachineRenameEditor(
                systemClient: systemClient,
                dismiss: { [weak self] in self?.popover?.performClose(nil) }
            )
        )
        self.popover = popover

        popover.show(relativeTo: anchorRect(in: titleBar, window: window),
                     of: titleBar,
                     preferredEdge: .minY)

        // A popover shown from the title bar does not take key status by itself,
        // and a text field in a window that is not key shows no insertion point:
        // the field would have the focus but look inert. Asked for after the
        // popover has been placed, so its window exists to be asked.
        DispatchQueue.main.async {
            popover.contentViewController?.view.window?.makeKey()
        }
    }

    func popoverDidClose(_ notification: Notification) {
        popover = nil
    }

    /// A point at the foot of the title bar, under the middle of the title.
    ///
    /// Horizontally from the title's own view, so the arrow lands beneath the
    /// words it is about to change. Vertically at the bottom of the bar rather
    /// than the bottom of the text, so the popover clears the subtitle -- which
    /// is where the duplicate-name warning lives, and covering that while
    /// renaming would hide the very reason for renaming.
    ///
    /// The title's view is undocumented and may not always be findable, in which
    /// case the anchor falls back to the middle of the bar: the popover then
    /// opens in a slightly less pointed place, which is a far better failure
    /// than not opening at all.
    private func anchorRect(in titleBar: NSView, window: NSWindow) -> NSRect {
        let x: CGFloat
        if let titleView = findTitleView(in: titleBar, title: window.title) {
            x = titleView.convert(titleView.bounds, to: titleBar).midX
        } else {
            x = titleBar.bounds.midX
        }
        return NSRect(x: x, y: titleBar.bounds.minY, width: 1, height: 1)
    }

    private func findTitleView(in view: NSView, title: String) -> NSView? {
        if let field = view as? NSTextField, field.stringValue == title, !title.isEmpty {
            return field
        }
        for subview in view.subviews {
            if let found = findTitleView(in: subview, title: title) {
                return found
            }
        }
        return nil
    }
}
