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

import SwiftUI
import AppKit

/// Shared actions for File menu commands. Captured from any view that has
/// @Environment(\.openWindow) and used by FileCommands via @ObservedObject.
/// This avoids @FocusedValue, which breaks when the key window changes
/// (e.g. after a menu interaction dismisses and the Welcome window loses focus).
@MainActor
class AppActions: ObservableObject {
    static let shared = AppActions()
    var openConnect: (() -> Void)? {
        willSet { objectWillChange.send() }
    }
    /// Show the Welcome window (the preset picker): focus the existing one if
    /// open, otherwise open one. Backs File > New (Cmd-N). It focus-or-opens so
    /// there is never more than one Welcome window.
    var showWelcome: (() -> Void)? {
        willSet { objectWillChange.send() }
    }
}

/// Enforces the "at most one Welcome window" invariant by real window identity.
///
/// A single WindowGroup renders the Welcome screen, and macOS state restoration
/// plus SwiftUI's auto-created window can otherwise yield two. Each Welcome
/// window registers here as it appears; a newcomer that finds a live Welcome
/// already registered is the loser and closes itself. `showWelcome` focuses the
/// registered window rather than opening another.
@MainActor
final class WelcomeWindowRegistry {
    static let shared = WelcomeWindowRegistry()

    /// Weak so a closed Welcome window is not kept alive. The slot is cleared
    /// actively on the window's close (below), so occupancy is not inferred from
    /// `isVisible` -- during state restoration a duplicate's window is not yet
    /// visible when it registers, which would let it slip past a visibility gate.
    private weak var welcomeWindow: NSWindow?
    private var closeObserver: NSObjectProtocol?

    /// Register `window` as the Welcome window. Returns true if it is the sole
    /// Welcome (the winner); false if another Welcome already holds the slot, in
    /// which case the caller should close `window` (deferred).
    func register(_ window: NSWindow) -> Bool {
        if let existing = welcomeWindow, existing !== window {
            return false
        }
        if welcomeWindow !== window {
            welcomeWindow = window
            closeObserver = NotificationCenter.default.addObserver(
                forName: NSWindow.willCloseNotification, object: window, queue: .main
            ) { [weak self, weak window] _ in
                Task { @MainActor in
                    guard let self, let window else { return }
                    self.clear(window)
                }
            }
        }
        return true
    }

    /// Drop `window` from the slot if it currently holds it (e.g. when a reused
    /// window stops showing the Welcome screen and starts showing a machine).
    func unregister(_ window: NSWindow) {
        clear(window)
    }

    private func clear(_ window: NSWindow) {
        guard welcomeWindow === window else { return }
        welcomeWindow = nil
        if let observer = closeObserver {
            NotificationCenter.default.removeObserver(observer)
            closeObserver = nil
        }
    }

    /// Bring the existing Welcome window forward. Returns false if none is open.
    @discardableResult
    func focusExisting() -> Bool {
        guard let window = welcomeWindow else { return false }
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        return true
    }
}

/// Posted when a new emulator window is about to open. The Welcome window
/// listens for this to close itself, regardless of which code path triggered
/// the new window (preset card, New Machine dialog, Connect dialog, etc.).
extension Notification.Name {
    static let didOpenEmulatorWindow = Notification.Name("BeebiumDidOpenEmulatorWindow")
}

// Renaming acts on the machine in the focused window, so it is a focused value
// rather than a shared action: it means nothing without a focused machine.
struct RenameMachineActionKey: FocusedValueKey {
    typealias Value = () -> Void
}

extension FocusedValues {
    var renameMachine: (() -> Void)? {
        get { self[RenameMachineActionKey.self] }
        set { self[RenameMachineActionKey.self] = newValue }
    }
}
