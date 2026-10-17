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


/// The rename editor: a plain-text field for the machine's name.
///
/// A machine's name is plain text, never a template (the templated string is
/// the machine's description, a separate thing shown as the window subtitle and
/// edited elsewhere). Only a machine not yet saved is renamed here; a saved
/// machine is named by its document, so the server refuses the rename and the
/// menu that opens this is disabled for it. The field shows the current name
/// and commits it with SetMachineName.
// Not private so a hosted test can open the real editor and observe its field.
struct MachineRenameEditor: View {
    @ObservedObject var systemClient: SystemClient
    let dismiss: () -> Void

    @State private var draft: String
    @State private var originalName: String
    @State private var isSaving = false
    @State private var saveError: String?

    @StateObject private var fieldController = TemplateFieldController()

    init(systemClient: SystemClient, dismiss: @escaping () -> Void) {
        self._systemClient = ObservedObject(wrappedValue: systemClient)
        self.dismiss = dismiss
        // Seed the field BEFORE it is created so it opens showing the current
        // name rather than empty.
        let name = systemClient.machineName
        self._draft = State(initialValue: name)
        self._originalName = State(initialValue: name)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            PopoverTextField(text: $draft,
                             placeholder: "Name",
                             controller: fieldController,
                             diagnosticsLabel: "rename",
                             onSubmit: { save() },
                             onCancel: { dismiss() })
                .frame(maxWidth: .infinity)
                .frame(height: 22)

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
        .onChange(of: draft) { _ in saveError = nil }
    }

    /// Save is offered only for a non-empty name that differs from the one the
    /// machine already has: an empty name is refused by the server and an
    /// unchanged one is a wasted round trip.
    private var isDirty: Bool {
        let trimmed = draft.trimmingCharacters(in: .whitespacesAndNewlines)
        return !trimmed.isEmpty && trimmed != originalName
    }

    /// Commit the name. On success the popover closes and the window title
    /// updates from the applied identity (and again from the status stream's
    /// IDENTITY_CHANGED). On failure the popover stays open and shows the reason
    /// (a saved machine is refused with the server's explanation), the way the
    /// station editor does.
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
    /// than the bottom of the text, so the popover clears the subtitle (the
    /// machine's description) rather than covering it while renaming.
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
