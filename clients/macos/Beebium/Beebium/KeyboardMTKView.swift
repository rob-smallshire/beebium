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

import AppKit
import MetalKit

/// Custom MTKView subclass that handles keyboard input.
///
/// MTKView doesn't accept keyboard input by default. This subclass overrides
/// the necessary methods to receive and process keyboard events, forwarding
/// them to the KeyboardClient for transmission to the emulator.
final class KeyboardMTKView: MTKView {

    /// Keyboard client for sending key events to the server
    weak var keyboardClient: KeyboardClient?

    // MARK: - First Responder

    override var acceptsFirstResponder: Bool {
        return true
    }

    override func becomeFirstResponder() -> Bool {
        return true
    }

    // MARK: - Keyboard Events

    override func keyDown(with event: NSEvent) {
        // Ignore macOS key repeat - BBC MOS handles its own auto-repeat
        if event.isARepeat {
            print("[KeyboardMTKView] keyDown REPEAT ignored: keyCode=\(event.keyCode)")
            return
        }

        guard let characters = event.characters else {
            print("[KeyboardMTKView] keyDown: no characters, keyCode=\(event.keyCode)")
            return
        }

        print("[KeyboardMTKView] keyDown: '\(characters)' keyCode=\(event.keyCode)")

        for char in characters {
            keyboardClient?.keyDown(character: char)
        }
    }

    override func keyUp(with event: NSEvent) {
        guard let characters = event.characters else {
            print("[KeyboardMTKView] keyUp: no characters, keyCode=\(event.keyCode)")
            return
        }

        print("[KeyboardMTKView] keyUp: '\(characters)' keyCode=\(event.keyCode)")

        for char in characters {
            keyboardClient?.keyUp(character: char)
        }
    }

    // MARK: - Focus Handling

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()

        // Become first responder when added to window
        if window != nil {
            DispatchQueue.main.async { [weak self] in
                self?.window?.makeFirstResponder(self)
            }
        }
    }
}
