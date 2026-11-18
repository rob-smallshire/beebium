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

/// The items of the live-input device picker, built fresh each time its submenu
/// opens (cassette round 12 follow-up). The picker is a native AppKit submenu
/// (see `CassetteBrowseMenu`), whose delegate calls this on open: SwiftUI
/// snapshots a `Menu`'s content when it opens and cannot populate a submenu
/// dynamically, and enumerating in a SwiftUI body ran at the deck's render and
/// asked the system for the microphone at launch. Enumerating here, on open,
/// means the deck's render touches no audio API and the open submenu shows the
/// current devices at once.
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
}
