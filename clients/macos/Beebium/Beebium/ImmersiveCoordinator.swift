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

extension Notification.Name {
    /// Posted by `ImmersiveCoordinator` when a window leaves native fullscreen by a path
    /// other than `exitImmersive(window:)` (e.g. Cmd+Ctrl+F, or the user clicking the
    /// green button if it ever becomes accessible). The notification's `object` is the
    /// `NSWindow` that exited. ContentView observes this so its `isImmersive` flag stays
    /// in sync with the window's actual fullscreen state.
    static let immersiveCoordinatorDidExitUnexpectedly =
        Notification.Name("ImmersiveCoordinatorDidExitUnexpectedly")
}

/// Manages Immersive Mode for Beebium machine windows.
///
/// Immersive Mode uses macOS native fullscreen (`toggleFullScreen(_:)`) for the
/// chrome-hiding window transformation: the window moves to its own Space, the menu bar
/// and Dock auto-hide, the window expands to fill the entire display with no rounded
/// corners or border artifacts, and AppKit handles screen disconnects natively.
///
/// We deliberately do not hide the title bar's title text or standard window buttons:
/// in native fullscreen the title bar reveals on hover at the top of the screen, and
/// hiding its content makes that reveal an empty translucent strip — uglier than just
/// showing the title and buttons, and pointless when the user already needs the menu
/// bar reachable.
///
/// The Beebium-specific behaviour layered on top of native fullscreen is:
///
///  * Hide the cursor after 3 s of idle activity, restored on any input event.
///  * Observe `NSWindow.didExitFullScreenNotification` so the caller (ContentView) is
///    told if fullscreen ends through any path other than our `exitImmersive` call,
///    and can flip its `isImmersive` flag accordingly.
@MainActor
final class ImmersiveCoordinator {
    static let shared = ImmersiveCoordinator()

    private init() {}

    private struct WindowState {
        weak var window: NSWindow?
        let titlebarSeparatorStyle: NSTitlebarSeparatorStyle
        var didExitObserver: NSObjectProtocol?
        var willCloseObserver: NSObjectProtocol?
    }

    private var states: [ObjectIdentifier: WindowState] = [:]
    private var presentationCount: Int = 0

    private var cursorEventMonitor: Any?
    private var cursorIdleTimer: Timer?
    private var cursorHidden: Bool = false
    private static let cursorIdleSeconds: TimeInterval = 3.0

    /// Enter Immersive Mode for `window`: install observers for fullscreen lifecycle and
    /// window close, start cursor auto-hide, and trigger native fullscreen.
    /// Idempotent for an already-immersive window.
    func enterImmersive(window: NSWindow) {
        let id = ObjectIdentifier(window)
        guard states[id] == nil else { return }

        var state = WindowState(
            window: window,
            titlebarSeparatorStyle: window.titlebarSeparatorStyle,
            didExitObserver: nil,
            willCloseObserver: nil
        )

        // Suppress the 1pt separator AppKit otherwise draws between title-bar and
        // content. In native fullscreen the title bar auto-retracts on hover-out, but
        // empirically a thin highlight appears at the top of the screen a few seconds
        // later — only on Beebium, not Terminal — which we suspect is this separator.
        window.titlebarSeparatorStyle = .none

        state.didExitObserver = NotificationCenter.default.addObserver(
            forName: NSWindow.didExitFullScreenNotification,
            object: window,
            queue: .main
        ) { [weak self] _ in
            // Synchronously, like the other callbacks here: the observer asks
            // for queue: .main, so assumeIsolated states what is already true
            // instead of deferring the work. Deferring matters -- until this
            // runs, states[id] still says the window is immersive, so an
            // enterImmersive() arriving in between would hit the idempotence
            // guard and be silently dropped, leaving the window out of
            // fullscreen with the UI believing it entered.
            MainActor.assumeIsolated {
                self?.handleNativeExit(id: id, window: window)
            }
        }

        state.willCloseObserver = NotificationCenter.default.addObserver(
            forName: NSWindow.willCloseNotification,
            object: window,
            queue: .main
        ) { [weak self] _ in
            MainActor.assumeIsolated {
                self?.handleWindowClosed(id: id)
            }
        }

        states[id] = state

        presentationCount += 1
        if presentationCount == 1 {
            startCursorAutoHide()
        }

        if !window.styleMask.contains(.fullScreen) {
            window.toggleFullScreen(nil)
        }
    }

    /// Exit Immersive Mode for `window`: tear down observers, restore the snapshotted
    /// title-bar separator style, and trigger the native fullscreen exit. No-op for a
    /// window that is not currently immersive.
    func exitImmersive(window: NSWindow) {
        let id = ObjectIdentifier(window)
        guard let state = states.removeValue(forKey: id) else { return }

        if let observer = state.didExitObserver {
            NotificationCenter.default.removeObserver(observer)
        }
        if let observer = state.willCloseObserver {
            NotificationCenter.default.removeObserver(observer)
        }

        window.titlebarSeparatorStyle = state.titlebarSeparatorStyle

        decrementPresentationCount()

        if window.styleMask.contains(.fullScreen) {
            window.toggleFullScreen(nil)
        }
    }

    func isImmersive(window: NSWindow) -> Bool {
        states[ObjectIdentifier(window)] != nil
    }

    /// Called when the window leaves fullscreen via a path other than `exitImmersive`.
    /// Tears down our state and posts a notification so ContentView can flip its
    /// `isImmersive` flag — without it, our flag would say "immersive" while the
    /// window is back in its windowed frame.
    private func handleNativeExit(id: ObjectIdentifier, window: NSWindow) {
        guard let state = states.removeValue(forKey: id) else { return }
        if let observer = state.didExitObserver {
            NotificationCenter.default.removeObserver(observer)
        }
        if let observer = state.willCloseObserver {
            NotificationCenter.default.removeObserver(observer)
        }

        window.titlebarSeparatorStyle = state.titlebarSeparatorStyle

        decrementPresentationCount()

        NotificationCenter.default.post(
            name: .immersiveCoordinatorDidExitUnexpectedly,
            object: window
        )
    }

    private func handleWindowClosed(id: ObjectIdentifier) {
        guard let state = states.removeValue(forKey: id) else { return }
        if let observer = state.didExitObserver {
            NotificationCenter.default.removeObserver(observer)
        }
        if let observer = state.willCloseObserver {
            NotificationCenter.default.removeObserver(observer)
        }
        decrementPresentationCount()
    }

    private func decrementPresentationCount() {
        presentationCount -= 1
        if presentationCount == 0 {
            stopCursorAutoHide()
        }
    }

    // MARK: - Cursor auto-hide

    private func startCursorAutoHide() {
        guard cursorEventMonitor == nil else { return }

        cursorEventMonitor = NSEvent.addLocalMonitorForEvents(
            matching: [.mouseMoved, .leftMouseDown, .rightMouseDown,
                       .otherMouseDown, .scrollWheel, .keyDown]
        ) { [weak self] event in
            // Local event monitors are called on the main thread, on the event
            // path. Handling the activity here rather than in a Task also
            // spares an allocation per mouse-moved event, which in Immersive
            // Mode is every frame the user moves the mouse.
            MainActor.assumeIsolated {
                self?.cursorActivity()
            }
            return event
        }

        // Start the idle countdown immediately so the cursor disappears
        // after cursorIdleSeconds even if the user stops moving the mouse
        // the moment they enter Immersive Mode.
        cursorActivity()
    }

    private func stopCursorAutoHide() {
        if let monitor = cursorEventMonitor {
            NSEvent.removeMonitor(monitor)
            cursorEventMonitor = nil
        }
        cursorIdleTimer?.invalidate()
        cursorIdleTimer = nil
        if cursorHidden {
            NSCursor.unhide()
            cursorHidden = false
        }
    }

    private func cursorActivity() {
        if cursorHidden {
            NSCursor.unhide()
            cursorHidden = false
        }
        cursorIdleTimer?.invalidate()
        cursorIdleTimer = Timer.scheduledTimer(
            withTimeInterval: Self.cursorIdleSeconds,
            repeats: false
        ) { [weak self] _ in
            // Scheduled on the main run loop, so it fires on the main thread.
            MainActor.assumeIsolated {
                self?.hideCursorNow()
            }
        }
    }

    private func hideCursorNow() {
        guard !cursorHidden, presentationCount > 0 else { return }
        NSCursor.hide()
        cursorHidden = true
    }
}
