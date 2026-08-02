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

/// A context menu on a window's title.
///
/// The title is where the machine's name is, so it is where a user reaches to
/// change it -- but macOS has no API for that. An editable title is an iOS
/// feature, and a title bar accessory cannot be placed over the title.
///
/// A right-click is used rather than a left-click because every left-click on
/// a title bar already means something: dragging moves the window,
/// double-clicking zooms or minimises it, and a single click on an inactive
/// window merely brings it forward. Acting on any of those would fire when
/// the user meant something else. A right-click on a title bar means nothing
/// here -- macOS shows a path menu only for document windows, which these are
/// not -- so it is free to be used.
///
/// The whole title bar is the target, not just the title text: locating the
/// text would mean hunting through an undocumented view hierarchy that would
/// fail silently the day it changes, and a right-click anywhere in the bar
/// means nothing else on a window with no represented file.
@MainActor
final class TitleClickMonitor {
    private var monitor: Any?
    private weak var window: NSWindow?
    private var menuTitle: String = ""
    private var action: (() -> Void)?

    /// Watch `window` and offer `menuTitle` when its title is right-clicked.
    func install(on window: NSWindow, menuTitle: String, action: @escaping () -> Void) {
        guard monitor == nil else { return }
        self.window = window
        self.menuTitle = menuTitle
        self.action = action

        monitor = NSEvent.addLocalMonitorForEvents(
            matching: [.rightMouseDown, .leftMouseDown]
        ) { [weak self] event in
            self?.handle(event) ?? event
        }
    }

    deinit {
        if let monitor {
            NSEvent.removeMonitor(monitor)
        }
    }

    /// Returns the event to pass on, or nil to swallow it.
    private func handle(_ event: NSEvent) -> NSEvent? {
        guard let window, event.window === window else { return event }

        // Control-click is the other way to ask for a context menu, and is
        // what a one-button mouse or a trackpad without secondary click uses.
        let isContextClick = event.type == .rightMouseDown
            || (event.type == .leftMouseDown && event.modifierFlags.contains(.control))
        guard isContextClick else { return event }

        guard isInTitleBar(event.locationInWindow) else { return event }

        showMenu(at: event)
        // Swallowed: the menu is the response to this click, and letting it
        // through as well would also bring up whatever else was listening.
        return nil
    }

    private func showMenu(at event: NSEvent) {
        guard let window else { return }
        let menu = NSMenu()
        let item = NSMenuItem(title: menuTitle,
                              action: #selector(performAction),
                              keyEquivalent: "")
        item.target = self
        menu.addItem(item)

        // Positioned against the title bar so the menu opens under the title
        // rather than wherever the pointer last was on screen.
        if let titleBar = window.standardWindowButton(.closeButton)?.superview {
            let point = titleBar.convert(event.locationInWindow, from: nil)
            menu.popUp(positioning: nil, at: point, in: titleBar)
        }
    }

    @objc private func performAction() {
        action?()
    }

    /// Whether a point in window coordinates is in the title bar.
    ///
    /// Anywhere in the bar, not only on the title text. Locating the text
    /// itself would mean hunting through the title bar's view hierarchy for
    /// whichever view happens to be drawing it, which is undocumented and
    /// would fail silently the day it changes. The bar as a whole is
    /// available from `contentLayoutRect`, and a right-click anywhere in it
    /// means nothing else on a window with no represented file, so the wider
    /// target costs nothing and is easier to hit.
    private func isInTitleBar(_ point: NSPoint) -> Bool {
        guard let window else { return false }
        // Window coordinates put the origin at the bottom left, so the title
        // bar is everything above the content.
        return point.y > window.contentLayoutRect.maxY
    }
}
