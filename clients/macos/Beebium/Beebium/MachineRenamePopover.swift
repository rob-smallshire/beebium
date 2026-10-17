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
/// edited elsewhere). Both kinds of machine rename here: a transient machine is
/// simply renamed; a saved machine's document is renamed in place by the server,
/// which moves its directory, so its recents entry and the window's represented
/// URL are updated afterwards. The field shows the current name and commits it
/// with SetMachineName.
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
    /// IDENTITY_CHANGED); a saved machine's moved directory is reflected in its
    /// recents entry and an open Machines page. On failure the popover stays
    /// open and shows the reason (a duplicate name, or the server's file-name
    /// complaint), the way the station editor does.
    private func save() {
        guard isDirty, !isSaving else { return }
        let trimmed = draft.trimmingCharacters(in: .whitespacesAndNewlines)
        let previousDirpath = systemClient.machineDirpath
        isSaving = true
        saveError = nil
        Task {
            let result = await systemClient.setMachineName(trimmed)
            isSaving = false
            switch result {
            case .success:
                noteRenameInRecents(from: previousDirpath)
                dismiss()
            case .failure(let error):
                saveError = error.localizedDescription
            }
        }
    }

    /// After a saved machine's document is renamed in place its directory moves,
    /// so move its recents entry from the old path to the new (the window's
    /// represented URL already follows systemClient.machineDirpath, re-fetched
    /// by setMachineName) and tell an open Machines page to reload. A transient
    /// machine has no saved directory, so there is nothing to record.
    private func noteRenameInRecents(from previousDirpath: String) {
        guard systemClient.machineSaved else { return }
        let newDirpath = systemClient.machineDirpath
        guard !previousDirpath.isEmpty, !newDirpath.isEmpty,
              previousDirpath != newDirpath else { return }
        MachineRecents().rename(from: URL(fileURLWithPath: previousDirpath),
                                to: URL(fileURLWithPath: newDirpath))
        NotificationCenter.default.post(name: .savedMachinesDidChange, object: nil)
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
