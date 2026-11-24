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
import XCTest
@testable import Beebium

/// The two window-lifecycle safety nets behind #309: the Welcome slot must free
/// synchronously the instant its window closes, and a closed connection window
/// must never be re-activated (which would come back blank).
@MainActor
final class WindowLifecycleRegistryTests: XCTestCase {
    private var windows: [NSWindow] = []

    private func makeWindow() -> NSWindow {
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 120, height: 80),
            styleMask: [.titled, .closable],
            backing: .buffered, defer: true)
        window.isReleasedWhenClosed = false  // we keep our own strong reference
        windows.append(window)
        return window
    }

    override func tearDown() {
        for window in windows where window.isVisible { window.orderOut(nil) }
        windows.removeAll()
        super.tearDown()
    }

    // MARK: - WelcomeWindowRegistry synchronous clear

    /// Closing the registered Welcome window frees the slot synchronously --
    /// before `close()` returns -- so a duplicate arriving on the same runloop
    /// turn becomes the winner, and nothing can focus the closing window in
    /// between. With the old deferred clear the slot still held the closing
    /// window at this point.
    func testSlotClearsSynchronouslyWhenRegisteredWindowCloses() {
        let registry = WelcomeWindowRegistry()
        let first = makeWindow()
        XCTAssertTrue(registry.register(first))

        // A second Welcome loses while the first holds the slot.
        let second = makeWindow()
        XCTAssertFalse(registry.register(second))

        first.close()

        XCTAssertFalse(registry.focusExisting(),
                       "the slot must be empty the instant the registered window closes")
        XCTAssertTrue(registry.register(second),
                      "a waiting window must be able to claim the freed slot on the same turn")
    }

    /// unregister frees the slot too (a reused window that becomes a machine
    /// window drops itself), and a later close of that same window is harmless.
    func testUnregisterFreesTheSlot() {
        let registry = WelcomeWindowRegistry()
        let window = makeWindow()
        XCTAssertTrue(registry.register(window))

        registry.unregister(window)
        XCTAssertFalse(registry.focusExisting())

        window.close()  // the willClose observer must be a no-op now
        XCTAssertFalse(registry.focusExisting())
    }

    // MARK: - ConnectionRegistry.activateWindow guard

    /// A closed (neither visible nor miniaturized) window is never activated:
    /// activateWindow reports no connection and drops the stale entry, so the
    /// caller opens a fresh window instead of reviving a dead, video-less one.
    func testActivateWindowSkipsAndClearsAClosedWindow() {
        let registry = ConnectionRegistry.shared
        let address = "test-closed.local:59991"
        let window = makeWindow()       // never ordered front -> not visible
        registry.register(address: address, window: window)
        XCTAssertFalse(window.isVisible)

        XCTAssertFalse(registry.activateWindow(for: address),
                       "a closed window must not be activated")
        XCTAssertNil(registry.window(for: address),
                     "the stale entry must be dropped so a fresh window opens")

        registry.unregister(address: address)  // belt-and-braces cleanup
    }

    /// A still-open window is activated normally and kept.
    func testActivateWindowActivatesAVisibleWindow() throws {
        let registry = ConnectionRegistry.shared
        let address = "test-open.local:59992"
        let window = makeWindow()
        window.makeKeyAndOrderFront(nil)
        try XCTSkipUnless(window.isVisible,
                          "test host could not show a window; skipping the positive path")
        registry.register(address: address, window: window)

        XCTAssertTrue(registry.activateWindow(for: address))
        XCTAssertNotNil(registry.window(for: address))

        registry.unregister(address: address)
    }
}
