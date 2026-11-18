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

/// An `NSMenuItem` that runs a closure when chosen, so a menu built in code
/// carries its action without a target/selector dance -- and a test can invoke
/// it directly.
final class BlockMenuItem: NSMenuItem {
    private let handler: () -> Void

    init(title: String, handler: @escaping () -> Void) {
        self.handler = handler
        super.init(title: title, action: #selector(run), keyEquivalent: "")
        target = self
    }

    required init(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    /// Run the item's action, as choosing it from the menu does.
    func invoke() { handler() }

    @objc private func run() { invoke() }
}

/// The live-input device picker, presented as an AppKit `NSMenu` built fresh
/// each time it opens (cassette round 12 follow-up).
///
/// It is not a SwiftUI submenu: SwiftUI snapshots a `Menu`'s content when the
/// menu opens, and a later asynchronous refresh never reaches the already-open
/// menu, so a deferred SwiftUI submenu showed only an empty list. Enumerating in
/// the SwiftUI body instead ran at the deck's render and asked the system for
/// the microphone at launch. An AppKit menu built on open resolves both: the
/// items are enumerated the moment the user opens the picker -- never at the
/// deck's render -- and the open menu shows them at once.
enum InputDeviceMenu {
    /// The menu items for the devices `list` reports right now: one per device,
    /// choosing it calls `choose`; or a single disabled item when enumeration
    /// found none. Calls `list.refresh()` exactly once, so the list is current
    /// as of this open. Pure of AppKit presentation, so it is tested.
    @MainActor
    static func items(from list: LiveInputDeviceList,
                      choose: @escaping (LiveAudioDevice) -> Void) -> [NSMenuItem] {
        list.refresh()
        guard !list.devices.isEmpty else {
            let empty = NSMenuItem(title: "No audio input devices", action: nil, keyEquivalent: "")
            empty.isEnabled = false
            return [empty]
        }
        return list.devices.map { device in
            BlockMenuItem(title: device.name) { choose(device) }
        }
    }

    /// Build the device menu from `list` and pop it up at the cursor. Deferred to
    /// the next run-loop tick so the SwiftUI menu that triggered it has dismissed
    /// first. The presentation is AppKit glue, not unit-tested; `items(from:)`
    /// carries the logic.
    @MainActor
    static func present(from list: LiveInputDeviceList,
                        choose: @escaping (LiveAudioDevice) -> Void) {
        let menu = NSMenu()
        menu.autoenablesItems = false
        for item in items(from: list, choose: choose) { menu.addItem(item) }
        let location = NSEvent.mouseLocation
        DispatchQueue.main.async {
            menu.popUp(positioning: nil, at: location, in: nil)
        }
    }
}
