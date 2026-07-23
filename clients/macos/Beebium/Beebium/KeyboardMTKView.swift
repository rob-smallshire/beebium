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
import Combine
import MetalKit

/// Custom MTKView subclass that handles keyboard input.
///
/// MTKView doesn't accept keyboard input by default. This subclass overrides
/// the necessary methods to receive and process keyboard events, forwarding
/// them to the KeyboardClient for transmission to the emulator.
final class KeyboardMTKView: MTKView, NSMenuItemValidation {

    /// Keyboard client for sending key events to the server
    weak var keyboardClient: KeyboardClient?

    /// Indicator client for LED states (used by Touch Bar)
    weak var indicatorClient: IndicatorClient?

    /// Touch Bar manager for creating and managing the Touch Bar
    var touchBarManager: BeebiumTouchBarManager?

    /// Types pasted text on the emulated keyboard
    weak var pasteCoordinator: PasteCoordinator?

    /// Reads the MODE 7 screen for copying
    weak var videoClient: VideoClient?

    /// Owns text selection and copy over the frozen display.
    weak var selectionCoordinator: SelectionCoordinator? {
        didSet { setUpSelectionOverlay() }
    }

    /// Draws the marquee and run highlights above the Metal frame.
    private let selectionOverlayView = SelectionOverlayView()

    /// Redraws the overlay whenever the selection or its highlights change.
    private var selectionObserver: AnyCancellable?

    /// Track previous modifier flags for change detection
    private var lastModifiers: NSEvent.ModifierFlags = []

    // MARK: - First Responder

    override var acceptsFirstResponder: Bool {
        return true
    }

    override func becomeFirstResponder() -> Bool {
        return true
    }

    // MARK: - Selection (Cmd-drag over the display)

    /// A Cmd-drag selects text; the qualifier held picks the interpretation. A
    /// plain drag is reserved for the machine (the future BBC mouse), and a
    /// plain click gives the machine back by dismissing any live selection.
    override func mouseDown(with event: NSEvent) {
        if event.modifierFlags.contains(.command), let coordinator = selectionCoordinator {
            coordinator.begin(
                atNormalizedPoint: normalizedPoint(for: event),
                interpretation: Self.selectionInterpretation(for: event.modifierFlags))
            return
        }
        if let coordinator = selectionCoordinator, coordinator.isSelecting {
            coordinator.cancel()
            return
        }
        super.mouseDown(with: event)
    }

    override func mouseDragged(with event: NSEvent) {
        if let coordinator = selectionCoordinator, coordinator.isSelecting {
            coordinator.update(toNormalizedPoint: normalizedPoint(for: event))
            return
        }
        super.mouseDragged(with: event)
    }

    /// The selection stays live after the mouse comes up, so the user can change
    /// the interpretation and copy. Escape or a plain click dismisses it.
    override func mouseUp(with event: NSEvent) {
        if let coordinator = selectionCoordinator, coordinator.isSelecting { return }
        super.mouseUp(with: event)
    }

    /// The interpretation a held modifier selects: none is rows, Option is a
    /// rectangle, Shift is anywhere. Shift wins if both are held.
    static func selectionInterpretation(for flags: NSEvent.ModifierFlags)
        -> ScreenSelectionInterpretation {
        if flags.contains(.shift) { return .anywhere }
        if flags.contains(.option) { return .rectangle }
        return .rows
    }

    /// A mouse event's location as a normalised, top-left-origin point over the
    /// view, which is what the coordinate mapper works in. The Metal view is not
    /// flipped (y up), so the vertical axis is inverted here.
    private func normalizedPoint(for event: NSEvent) -> CGPoint {
        let local = convert(event.locationInWindow, from: nil)
        let width = max(bounds.width, 1)
        let height = max(bounds.height, 1)
        return CGPoint(x: local.x / width, y: 1 - local.y / height)
    }

    // MARK: - Selection overlay

    private func setUpSelectionOverlay() {
        if selectionOverlayView.superview == nil {
            selectionOverlayView.frame = bounds
            selectionOverlayView.autoresizingMask = [.width, .height]
            // Layer-backed so it composites cleanly above the Metal layer.
            selectionOverlayView.wantsLayer = true
            addSubview(selectionOverlayView)
        }
        selectionCoordinator?.viewSize = bounds.size
        selectionObserver = selectionCoordinator?.objectWillChange
            .receive(on: RunLoop.main)
            .sink { [weak self] in self?.refreshSelectionOverlay() }
        refreshSelectionOverlay()
    }

    private func refreshSelectionOverlay() {
        guard let coordinator = selectionCoordinator, coordinator.isSelecting,
              let geometry = coordinator.mappingGeometry else {
            selectionOverlayView.isHidden = true
            selectionOverlayView.rangeRects = []
            selectionOverlayView.highlightRects = []
            selectionOverlayView.marqueeRect = nil
            return
        }
        selectionOverlayView.isHidden = false
        let size = bounds.size
        selectionOverlayView.rangeRects = coordinator.rangeRects.map {
            ScreenCoordinateMapper.viewRect(frameRect: $0, geometry: geometry, viewSize: size)
        }
        selectionOverlayView.highlightRects = coordinator.highlightRects.map {
            ScreenCoordinateMapper.viewRect(frameRect: $0, geometry: geometry, viewSize: size)
        }
        selectionOverlayView.marqueeRect = coordinator.marqueeRect.map {
            ScreenCoordinateMapper.viewRect(frameRect: $0, geometry: geometry, viewSize: size)
        }
    }

    override func setFrameSize(_ newSize: NSSize) {
        super.setFrameSize(newSize)
        // The overlay and the picture must be laid out by one geometry, so the
        // coordinator is told the window's shape before anything is mapped
        // with it -- otherwise a resize mid-selection leaves the highlights
        // fitted to the window as it was.
        selectionCoordinator?.viewSize = newSize
        refreshSelectionOverlay()
    }

    // MARK: - Keyboard Events

    override func performKeyEquivalent(with event: NSEvent) -> Bool {
        // Let command-key combinations propagate to the menu system
        if event.modifierFlags.contains(.command) {
            return super.performKeyEquivalent(with: event)
        }
        return false
    }

    override func keyDown(with event: NSEvent) {
        // Command-key combinations are handled by performKeyEquivalent for menu shortcuts
        if event.modifierFlags.contains(.command) {
            return
        }

        // Ignore macOS key repeat - BBC MOS handles its own auto-repeat
        if event.isARepeat {
            return
        }

        // Escape dismisses a live selection and is swallowed, the same rule the
        // paste feature follows below: while a host interaction owns the screen,
        // Escape ends that, and only reaches the Beeb when there is nothing
        // host-side to cancel.
        if event.keyCode == MacKeyCode.escape, let coordinator = selectionCoordinator,
           coordinator.isSelecting {
            coordinator.cancel()
            return
        }

        // Escape stops a paste that is still being typed, and is swallowed
        // rather than passed on: while text is arriving, Escape means "stop
        // this", not "interrupt whatever the machine is running". With no
        // paste in flight it reaches the BBC as usual.
        //
        // Characters already typed stay where they are -- the queue is what is
        // discarded, not the keystrokes the machine has already seen.
        if event.keyCode == MacKeyCode.escape, let coordinator = pasteCoordinator,
           coordinator.isPasting {
            Task { @MainActor in await coordinator.cancel() }
            return
        }

        let input = KeyInput(event: event, source: .physicalKeyboard)
        print("[KBD][in.keyDown] keyCode=\(event.keyCode)"
              + " chars=\(quotedDebug(input.characters))"
              + " charsIgnoringMods=\(quotedDebug(input.charactersIgnoringModifiers))"
              + " mods=[\(formatModifiers(event.modifierFlags))]")

        keyboardClient?.keyDown(input: input)
    }

    override func keyUp(with event: NSEvent) {
        let input = KeyInput(event: event, source: .physicalKeyboard)
        print("[KBD][in.keyUp] keyCode=\(event.keyCode)"
              + " chars=\(quotedDebug(input.characters))"
              + " mods=[\(formatModifiers(event.modifierFlags))]")

        keyboardClient?.keyUp(input: input)
    }

    override func flagsChanged(with event: NSEvent) {
        let current = event.modifierFlags
        let diff = current.symmetricDifference(lastModifiers)
        let keyCode = event.keyCode

        print("[KBD][in.flags] keyCode=\(keyCode)"
              + " current=[\(formatModifiers(current))]"
              + " prev=[\(formatModifiers(lastModifiers))]"
              + " diff=[\(formatModifiers(diff))]")

        // While a host drag owns the screen, further modifiers qualify the
        // selection rather than reaching the machine. Update the interpretation
        // and swallow the change so no spurious modifier press or release is
        // forwarded to the emulated keyboard.
        if let coordinator = selectionCoordinator, coordinator.ownsQualifierModifiers {
            coordinator.setInterpretation(Self.selectionInterpretation(for: current))
            lastModifiers = current
            return
        }

        // Caps Lock uses special handling (toggle sync)
        if diff.contains(.capsLock) {
            keyboardClient?.handleCapsLockToggle()
        }

        // Other modifiers: route through normal mapping system
        // This allows modifier keys to be mapped to BBC keys via keyMappings
        if isModifierKeyCode(keyCode) && keyCode != MacKeyCode.capsLock {
            let isDown = isModifierPressed(keyCode: keyCode, flags: current)
            let input = KeyInput(keyCode: keyCode, source: .physicalKeyboard)
            print("[KBD][in.flags] -> keyCode=\(keyCode) isDown=\(isDown)")

            if isDown {
                keyboardClient?.keyDown(input: input)
            } else {
                keyboardClient?.keyUp(input: input)
            }
        }

        lastModifiers = current
    }

    /// Render NSEvent.ModifierFlags as a readable comma-separated list.
    private func formatModifiers(_ flags: NSEvent.ModifierFlags) -> String {
        var parts: [String] = []
        if flags.contains(.shift)    { parts.append("shift") }
        if flags.contains(.control)  { parts.append("control") }
        if flags.contains(.option)   { parts.append("option") }
        if flags.contains(.command)  { parts.append("command") }
        if flags.contains(.function) { parts.append("function") }
        if flags.contains(.capsLock) { parts.append("capsLock") }
        if flags.contains(.numericPad) { parts.append("numericPad") }
        if flags.contains(.help)     { parts.append("help") }
        return parts.joined(separator: ",")
    }

    /// Quote a string with visible escapes so log lines remain on one line.
    private func quotedDebug(_ s: String) -> String {
        var out = "\""
        for scalar in s.unicodeScalars {
            switch scalar {
            case "\"": out += "\\\""
            case "\\": out += "\\\\"
            case "\n": out += "\\n"
            case "\r": out += "\\r"
            case "\t": out += "\\t"
            default:
                if scalar.value < 0x20 || scalar.value == 0x7F {
                    out += String(format: "\\u{%04X}", scalar.value)
                } else {
                    out += String(scalar)
                }
            }
        }
        out += "\""
        return out
    }

    /// Check if a keyCode is a modifier key
    private func isModifierKeyCode(_ keyCode: UInt16) -> Bool {
        switch keyCode {
        case MacKeyCode.shift, MacKeyCode.rightShift,
             MacKeyCode.control, MacKeyCode.rightControl,
             MacKeyCode.option, MacKeyCode.rightOption,
             MacKeyCode.command, MacKeyCode.rightCommand,
             MacKeyCode.function, MacKeyCode.capsLock:
            return true
        default:
            return false
        }
    }

    /// Determine if a specific modifier key is pressed based on its keyCode and current flags
    private func isModifierPressed(keyCode: UInt16, flags: NSEvent.ModifierFlags) -> Bool {
        switch keyCode {
        case MacKeyCode.shift, MacKeyCode.rightShift:
            return flags.contains(.shift)
        case MacKeyCode.control, MacKeyCode.rightControl:
            return flags.contains(.control)
        case MacKeyCode.option, MacKeyCode.rightOption:
            return flags.contains(.option)
        case MacKeyCode.command, MacKeyCode.rightCommand:
            return flags.contains(.command)
        case MacKeyCode.function:
            return flags.contains(.function)
        default:
            return false
        }
    }

    /// Reset modifier tracking to current state (call on window focus gain)
    func resetModifierTracking() {
        lastModifiers = NSEvent.modifierFlags
    }

    // MARK: - Edit Menu

    /// Type the clipboard's text on the emulated keyboard.
    ///
    /// The emulator has no text cursor to insert at, so pasting means typing:
    /// the characters go through the keyboard matrix exactly as if they had
    /// been typed by hand. That works wherever typing works -- in BASIC, in a
    /// text editor, at a game's name prompt -- rather than only where the
    /// operating system happens to be reading a line.
    ///
    /// Long pastes take a while, because the machine can only accept keys as
    /// fast as the MOS scans them. That is the machine's speed, not ours.
    @objc func paste(_ sender: Any?) {
        guard let text = NSPasteboard.general.string(forType: .string) else {
            // Non-text contents (an image, a file promise). Nothing to type.
            NSSound.beep()
            return
        }

        guard !text.isEmpty else { return }

        guard let pasteCoordinator = pasteCoordinator else {
            print("[KeyboardMTKView] paste: no paste coordinator, ignoring")
            NSSound.beep()
            return
        }

        Task { @MainActor in
            if let reason = await pasteCoordinator.paste(text) {
                presentPasteFailure(reason)
            }
        }
    }

    /// Copy the screen's text to the clipboard: the selection in aligned-rows
    /// reading when one is live, or the whole screen aligned when there is not.
    ///
    /// Whatever mode the machine is in, the server reads the text and returns
    /// it; the client never learns the mode. The aligned-rectangle and anywhere
    /// variants are their own Edit-menu commands (Option-Cmd-C, Shift-Cmd-C).
    @objc func copy(_ sender: Any?) {
        guard let coordinator = selectionCoordinator else {
            NSSound.beep()
            return
        }
        Task { @MainActor in
            if await coordinator.copy(.rows) == nil {
                // Read, but nothing on screen was text -- a graphics-only
                // display. Say so rather than silently putting nothing on the
                // clipboard, which would look like the command had worked.
                presentNothingToCopy()
            }
        }
    }

    @MainActor
    private func presentNothingToCopy() {
        guard let window = window else { return }
        let alert = NSAlert()
        alert.messageText = "Nothing to copy"
        alert.informativeText =
            "No text was found on the screen to copy."
        alert.alertStyle = .informational
        alert.addButton(withTitle: "OK")
        alert.beginSheetModal(for: window, completionHandler: nil)
    }

    /// Enable Paste only when there is text to paste and somewhere to put it.
    ///
    /// Without this the Edit menu's Paste item is enabled whenever this view is
    /// first responder, which is always, and choosing it when disconnected
    /// would do nothing with no explanation.
    func validateMenuItem(_ menuItem: NSMenuItem) -> Bool {
        if menuItem.action == #selector(copy(_:)) {
            return selectionCoordinator != nil
        }
        if menuItem.action == #selector(paste(_:)) {
            let hasText = NSPasteboard.general.canReadObject(
                forClasses: [NSString.self], options: nil)
            return hasText && pasteCoordinator != nil
        }
        // Anything else reaching this view is not ours to judge. Menu items
        // with no validator default to enabled, which is AppKit's behaviour
        // for a responder that does not implement validation at all.
        return true
    }

    @MainActor
    private func presentPasteFailure(_ reason: String) {
        guard let window = window else {
            print("[KeyboardMTKView] paste failed with no window: \(reason)")
            return
        }

        let alert = NSAlert()
        alert.messageText = "Could not paste"
        alert.informativeText = reason
        alert.alertStyle = .warning
        alert.addButton(withTitle: "OK")
        alert.beginSheetModal(for: window, completionHandler: nil)
    }

    // MARK: - Focus Handling

    /// Observer for window becoming key (focus gained)
    private var windowDidBecomeKeyObserver: NSObjectProtocol?

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()

        // Remove old observer if any
        if let observer = windowDidBecomeKeyObserver {
            NotificationCenter.default.removeObserver(observer)
            windowDidBecomeKeyObserver = nil
        }

        if let window = window {
            // Become first responder when added to window
            DispatchQueue.main.async { [weak self] in
                window.makeFirstResponder(self)
            }

            // Observe window becoming key to reset modifier tracking
            windowDidBecomeKeyObserver = NotificationCenter.default.addObserver(
                forName: NSWindow.didBecomeKeyNotification,
                object: window,
                queue: .main
            ) { [weak self] _ in
                self?.resetModifierTracking()
            }
        }
    }

    deinit {
        if let observer = windowDidBecomeKeyObserver {
            NotificationCenter.default.removeObserver(observer)
        }
    }

    // MARK: - Touch Bar Support

    @available(macOS 10.12.2, *)
    override func makeTouchBar() -> NSTouchBar? {
        return touchBarManager?.getTouchBar()
    }
}
