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

/// Holds a weak reference to the live NSTextField behind the template editor so
/// the placeholder picker can insert text at the caret. SwiftUI's `TextField`
/// exposes no caret, so the editor uses an AppKit field and reaches it through
/// this controller.
@MainActor
final class TemplateFieldController: ObservableObject {
    weak var field: NSTextField?

    /// Insert `insertion` at the field's caret, or over its current selection,
    /// then update `text` and leave the caret just after the inserted text with
    /// focus held. Falls back to appending if the field is not available yet.
    func insertAtCaret(_ insertion: String, text: Binding<String>) {
        guard let field else {
            text.wrappedValue += insertion
            return
        }
        let current = field.stringValue
        let selected = field.currentEditor()?.selectedRange
            ?? NSRange(location: (current as NSString).length, length: 0)
        let range = selected.location ..< (selected.location + selected.length)
        let (newText, caret) = NameTemplateEditing.insert(
            insertion, into: current, replacingUTF16: range)
        field.stringValue = newText
        text.wrappedValue = newText
        field.window?.makeFirstResponder(field)
        field.currentEditor()?.selectedRange = NSRange(location: caret, length: 0)
    }
}

/// A single-line AppKit text field for the template, wrapped for SwiftUI so the
/// picker can know the caret. Return commits, Escape abandons.
private struct TemplateTextField: NSViewRepresentable {
    @Binding var text: String
    let controller: TemplateFieldController
    let onSubmit: () -> Void
    let onCancel: () -> Void

    func makeNSView(context: Context) -> NSTextField {
        let field = NSTextField()
        field.placeholderString = "Name template"
        field.stringValue = text
        field.delegate = context.coordinator
        field.usesSingleLineMode = true
        field.cell?.wraps = false
        field.cell?.isScrollable = true
        // Draw an opaque bezeled field. Inside a popover's vibrant material a
        // field with no background lets the cell draw its own (unscrolled) text
        // while the field editor draws the scrolled text over it -- two copies at
        // different offsets once the caret reaches the right end. An opaque
        // background masks the cell so only the field editor shows.
        field.isBezeled = true
        field.bezelStyle = .roundedBezel
        field.drawsBackground = true
        field.backgroundColor = .textBackgroundColor
        controller.field = field
        // Take focus once the hosting window is key; the insertion point only
        // blinks in the key window, so focusing before the window is key leaves a
        // field that looks focused but shows no caret.
        DispatchQueue.main.async { [weak field] in
            guard let field, let window = field.window else { return }
            window.makeKey()
            window.makeFirstResponder(field)
        }
        return field
    }

    func updateNSView(_ nsView: NSTextField, context: Context) {
        if nsView.stringValue != text {
            nsView.stringValue = text
        }
        context.coordinator.parent = self
    }

    func makeCoordinator() -> Coordinator { Coordinator(self) }

    final class Coordinator: NSObject, NSTextFieldDelegate {
        var parent: TemplateTextField
        init(_ parent: TemplateTextField) { self.parent = parent }

        func controlTextDidChange(_ obj: Notification) {
            guard let field = obj.object as? NSTextField else { return }
            parent.text = field.stringValue
        }

        func control(_ control: NSControl,
                     textView: NSTextView,
                     doCommandBy commandSelector: Selector) -> Bool {
            switch commandSelector {
            case #selector(NSResponder.insertNewline(_:)):
                parent.onSubmit()
                return true
            case #selector(NSResponder.cancelOperation(_:)):
                parent.onCancel()
                return true
            default:
                return false
            }
        }
    }
}

/// The rename editor: the machine's name *template*, a picker of the server's
/// placeholders, and a live preview of the rendered name (#153).
///
/// The field shows the template, not the rendered name, because the template is
/// what the user is changing. The picker, its groups, labels, values and the
/// `{key}` insertion text all come from the server, so the app carries no list
/// of placeholder keys. If the server is too old to offer the placeholder and
/// preview RPCs, this degrades to a plain name field with neither picker nor
/// preview.
private struct MachineRenameEditor: View {
    @ObservedObject var systemClient: SystemClient
    let dismiss: () -> Void

    @State private var draft: String = ""
    @State private var originalTemplate: String = ""
    @State private var placeholders: [Beebium_NamePlaceholder] = []
    @State private var pickerAvailable = false
    @State private var previewAvailable = false
    @State private var preview: String = ""
    @State private var note: String = ""
    @State private var didLoad = false

    @StateObject private var fieldController = TemplateFieldController()
    @State private var previewDebouncer = Debouncer(delay: 0.25)

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            TemplateTextField(text: $draft,
                              controller: fieldController,
                              onSubmit: { commit() },
                              onCancel: { dismiss() })
                .frame(maxWidth: .infinity)

            if pickerAvailable {
                placeholderPicker
            }

            if previewAvailable {
                Divider()
                previewSection
            }
        }
        .padding(12)
        .frame(width: 440)
        .activatesHostingWindow()
        .onAppear(perform: load)
        .onDisappear { previewDebouncer.cancel() }
        .onChange(of: draft) { _ in
            previewDebouncer.schedule {
                Task { await refreshPreview() }
            }
        }
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
        // Show the template. An empty template means a server too old to carry
        // one; fall back to the rendered name so the plain field is not blank.
        let template = systemClient.machineNameTemplate.isEmpty
            ? systemClient.machineName
            : systemClient.machineNameTemplate
        draft = template
        originalTemplate = template
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

    private func commit() {
        let trimmed = draft.trimmingCharacters(in: .whitespacesAndNewlines)
        // An empty template is refused by the server, and an unchanged one is a
        // wasted round trip.
        if !trimmed.isEmpty, trimmed != originalTemplate {
            systemClient.setMachineName(trimmed)
        }
        dismiss()
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
