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
import SwiftUI

/// Tracks active connections across all windows
@MainActor
class ConnectionRegistry: ObservableObject {
    static let shared = ConnectionRegistry()

    /// Active connections: address -> window
    @Published private(set) var connections: [String: NSWindow] = [:]

    private init() {}

    /// Register a connection for a window
    func register(address: String, window: NSWindow) {
        connections[address] = window
        NSLog("[ConnectionRegistry] Registered connection to \(address)")
    }

    /// Unregister a connection
    func unregister(address: String) {
        connections.removeValue(forKey: address)
        NSLog("[ConnectionRegistry] Unregistered connection to \(address)")
    }

    /// Check if we're connected to an address
    func isConnected(to address: String) -> Bool {
        if let window = connections[address] {
            // Verify window is still valid
            return window.isVisible || window.isMiniaturized
        }
        return false
    }

    /// Check if we're connected to a target
    func isConnected(to target: ConnectionTarget) -> Bool {
        isConnected(to: target.address)
    }

    /// Get the window for a connection, if any
    func window(for address: String) -> NSWindow? {
        connections[address]
    }

    /// Activate (bring to front) the window for a connection
    /// Returns true if a window was activated, false if no connection exists
    ///
    /// A closed window must never be activated: it would come back empty, with
    /// no live video view (the second symptom of #309 -- a blank window on the
    /// attempt after a lost one). A registered window that is neither visible
    /// nor miniaturized has been closed; drop the stale entry and report no
    /// connection, so the caller opens a fresh window instead of reviving a dead
    /// one.
    func activateWindow(for address: String) -> Bool {
        guard let window = connections[address] else {
            return false
        }
        guard window.isVisible || window.isMiniaturized else {
            unregister(address: address)
            return false
        }

        if window.isMiniaturized {
            window.deminiaturize(nil)
        }
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)

        NSLog("[ConnectionRegistry] Activated window for \(address)")
        return true
    }

    /// Activate window for a target
    func activateWindow(for target: ConnectionTarget) -> Bool {
        activateWindow(for: target.address)
    }

    /// Clean up stale entries (windows that have been closed)
    func cleanup() {
        let staleAddresses = connections.filter { _, window in
            !window.isVisible && !window.isMiniaturized
        }.map { $0.key }

        for address in staleAddresses {
            connections.removeValue(forKey: address)
            NSLog("[ConnectionRegistry] Cleaned up stale connection to \(address)")
        }
    }
}
