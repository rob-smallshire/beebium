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

/// Shared actions for File menu commands. Captured from any view that has
/// @Environment(\.openWindow) and used by FileCommands via @ObservedObject.
/// This avoids @FocusedValue, which breaks when the key window changes
/// (e.g. after a menu interaction dismisses and the Welcome window loses focus).
@MainActor
class AppActions: ObservableObject {
    static let shared = AppActions()
    var openNewMachine: (() -> Void)? {
        willSet { objectWillChange.send() }
    }
    var openConnect: (() -> Void)? {
        willSet { objectWillChange.send() }
    }
    var openWelcome: (() -> Void)? {
        willSet { objectWillChange.send() }
    }
}

/// Posted when a new emulator window is about to open. The Welcome window
/// listens for this to close itself, regardless of which code path triggered
/// the new window (preset card, New Machine dialog, Connect dialog, etc.).
extension Notification.Name {
    static let didOpenEmulatorWindow = Notification.Name("BeebiumDidOpenEmulatorWindow")
}

// "New Window" remains a focused value — it should only be available from emulator windows.
struct OpenNewWindowActionKey: FocusedValueKey {
    typealias Value = () -> Void
}

// Renaming acts on the machine in the focused window, so like New Window it is
// a focused value rather than a shared action: it means nothing without one.
struct RenameMachineActionKey: FocusedValueKey {
    typealias Value = () -> Void
}

extension FocusedValues {
    var openNewWindow: (() -> Void)? {
        get { self[OpenNewWindowActionKey.self] }
        set { self[OpenNewWindowActionKey.self] = newValue }
    }

    var renameMachine: (() -> Void)? {
        get { self[RenameMachineActionKey.self] }
        set { self[RenameMachineActionKey.self] = newValue }
    }
}
