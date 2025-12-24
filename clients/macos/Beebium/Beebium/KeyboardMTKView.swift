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
