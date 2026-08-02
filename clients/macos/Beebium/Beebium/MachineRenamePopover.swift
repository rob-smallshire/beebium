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

/// The rename field itself: one line, and nothing else.
///
/// Renaming a machine is a small act, so it gets a small control. Return
/// commits, Escape abandons, and clicking away does what clicking away
/// usually does -- there are no buttons to press because there is nothing a
/// button would say that the field does not.
private struct MachineRenameField: View {
    let currentName: String
    let rename: (String) -> Void
    let dismiss: () -> Void

    @State private var draft: String = ""
    @FocusState private var focused: Bool

    var body: some View {
        TextField("Machine name", text: $draft)
            .textFieldStyle(.roundedBorder)
            .focused($focused)
            .frame(width: 200)
            .onSubmit {
                let trimmed = draft.trimmingCharacters(in: .whitespacesAndNewlines)
                // An empty name is refused by the server, and an unchanged one
                // is a wasted round trip.
                if !trimmed.isEmpty, trimmed != currentName {
                    rename(trimmed)
                }
                dismiss()
            }
            .onExitCommand { dismiss() }
            .padding(10)
            .onAppear {
                draft = currentName
                focused = true
            }
    }
}

/// Shows the rename field in a popover pointing at the window's title.
///
/// A popover rather than a sheet: a sheet takes the whole window hostage for
/// a single short string, and says nothing about which window's machine is
/// being renamed. A popover points at the name it is about to change.
@MainActor
final class MachineRenamePopover: NSObject, NSPopoverDelegate {
    private var popover: NSPopover?

    func show(in window: NSWindow, currentName: String, rename: @escaping (String) -> Void) {
        // A second request while one is open should not stack popovers; the
        // one already there is the one the user asked for.
        if popover?.isShown == true { return }

        guard let titleBar = window.standardWindowButton(.closeButton)?.superview else {
            return
        }

        let popover = NSPopover()
        popover.behavior = .transient
        popover.delegate = self
        popover.contentViewController = NSHostingController(
            rootView: MachineRenameField(
                currentName: currentName,
                rename: rename,
                dismiss: { [weak self] in self?.popover?.performClose(nil) }
            )
        )
        self.popover = popover

        popover.show(relativeTo: anchorRect(in: titleBar, window: window),
                     of: titleBar,
                     preferredEdge: .minY)
    }

    func popoverDidClose(_ notification: Notification) {
        popover = nil
    }

    /// A point at the foot of the title bar, under the middle of the title.
    ///
    /// Horizontally from the title's own view, so the arrow lands beneath the
    /// words it is about to change. Vertically at the bottom of the bar
    /// rather than the bottom of the text, so the popover clears the subtitle
    /// -- which is where the duplicate-name warning lives, and covering that
    /// while renaming would hide the very reason for renaming.
    ///
    /// The title's view is undocumented and may not always be findable, in
    /// which case the anchor falls back to the middle of the bar: the popover
    /// then opens in a slightly less pointed place, which is a far better
    /// failure than not opening at all.
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
