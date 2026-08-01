// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

//
//  BeebiumTouchBarEscapeKey.swift
//  Beebium
//
//  Escape key replacement functionality for Gen 1 Touch Bars
//

import Cocoa
import Foundation

// MARK: - BeebiumTouchBarEscapeKey Component Class

@available(macOS 10.12.2, *)
@MainActor
class BeebiumTouchBarEscapeKey: BeebiumTouchBarComponent {

    // MARK: - Properties

    // TouchBar identifier for this component
    private let identifier: NSTouchBarItem.Identifier

    // Keyboard client for sending key events
    private weak var keyboardClient: KeyboardClient?

    // BBC key cache for key lookups
    private weak var bbcKeyCache: BBCKeyCache?

    // MARK: - Initialization

    init(identifier: NSTouchBarItem.Identifier, keyboardClient: KeyboardClient?, bbcKeyCache: BBCKeyCache?) {
        self.identifier = identifier
        self.keyboardClient = keyboardClient
        self.bbcKeyCache = bbcKeyCache
    }

    // MARK: - BeebiumTouchBarComponent Protocol Implementation

    var componentName: String { "EscapeKey" }

    func createTouchBarItem() -> NSCustomTouchBarItem {
        let item = NSCustomTouchBarItem(identifier: identifier)

        // Create button with styled appearance matching BREAK key
        let button = NSButton(title: "ESCAPE", target: self, action: #selector(escapeKeyPressed(_:)))

        // Set background color to match BREAK key using centralized constants
        button.bezelColor = BeebiumAppearance.Colors.regularKeyColor

        // Configure font to match other keys using centralized constants
        if let font = NSFont.systemFont(ofSize: BeebiumAppearance.Typography.keyTextSize,
                                         weight: BeebiumAppearance.Typography.keyTextWeight) as NSFont? {
            button.font = font
        }

        item.view = button
        item.customizationLabel = "BBC Micro ESCAPE Key"

        return item
    }

    func updateState() {
        // Escape key has no state to update
    }

    // MARK: - Event Handling

    /// Handle escape key button press
    @objc private func escapeKeyPressed(_ sender: NSButton) {
        // Look up the Escape key in the BBC key cache
        guard let entry = bbcKeyCache?.lookup(name: "Escape") else {
            print("[TouchBar] Escape key not found in cache")
            return
        }

        // Press now, so the key is registered as held before this returns; the
        // release is the only part that has to wait.
        keyboardClient?.touchBarKeyDown(bbcKeyName: "Escape", ikNumber: entry.ikNumber)

        Task { @MainActor in
            // Send key up event after a brief delay to avoid state confusion
            // The BBC MOS maintains internal state about pressed keys,
            // and sending up immediately after down can cause the up event to fail
            try? await Task.sleep(nanoseconds: 50_000_000) // 50ms

            // A focus loss during the delay will already have released it, and
            // this becomes a no-op rather than a stray key-up.
            self.keyboardClient?.touchBarKeyUp(bbcKeyName: "Escape")
        }
    }
}
