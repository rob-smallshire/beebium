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

import Foundation

extension Notification.Name {
    /// Posted (main thread) when the host is about to enter a full system sleep.
    static let beebiumHostWillSleep = Notification.Name("beebiumHostWillSleep")
    /// Posted (main thread) after the host wakes from a full system sleep, when
    /// a live connection is very likely dead and should be re-established.
    static let beebiumHostDidWake = Notification.Name("beebiumHostDidWake")
}

/// Platform-abstracted host power-state events.
///
/// A front-end on any platform observes when the host machine is about to
/// sleep and when it has woken, and drives connection recovery from those.
/// The connection layer consumes this protocol and never the OS API behind it,
/// so porting to another platform means providing one conformer, not reworking
/// the recovery logic. See docs/frontend-sleep-wake.md for the contract other
/// front-ends must honour.
///
/// Contract:
/// - Callbacks are always delivered on the main thread.
/// - `onDidWake` signals a wake from a *full system sleep* -- the process was
///   suspended, so any live network connection is very likely dead and must be
///   re-established. It does NOT fire for a mere display sleep, where the
///   process keeps running and the connection survives.
/// - `onWillSleep` signals an imminent full system sleep, while the process is
///   still running -- a chance to stop futile work (e.g. a reconnect backoff)
///   that would only run once on wake anyway.
protocol SystemPowerMonitoring: AnyObject {
    var onWillSleep: (() -> Void)? { get set }
    var onDidWake: (() -> Void)? { get set }
    func start()
    func stop()
}

#if canImport(AppKit)
import AppKit

/// macOS `SystemPowerMonitoring` over `NSWorkspace` sleep/wake notifications.
///
/// These are posted on `NSWorkspace.shared.notificationCenter` -- distinct from
/// the default `NotificationCenter` -- and only for full system sleep, which is
/// exactly the case that drops the connection. Display sleep is not reported
/// here and needs no reconnect (the video path presents on frame arrival).
final class MacSystemPowerMonitor: SystemPowerMonitoring {
    var onWillSleep: (() -> Void)?
    var onDidWake: (() -> Void)?

    private var observers: [NSObjectProtocol] = []

    func start() {
        guard observers.isEmpty else { return }
        let center = NSWorkspace.shared.notificationCenter
        observers.append(center.addObserver(
            forName: NSWorkspace.willSleepNotification, object: nil, queue: .main
        ) { [weak self] _ in
            self?.onWillSleep?()
        })
        observers.append(center.addObserver(
            forName: NSWorkspace.didWakeNotification, object: nil, queue: .main
        ) { [weak self] _ in
            self?.onDidWake?()
        })
    }

    func stop() {
        let center = NSWorkspace.shared.notificationCenter
        for observer in observers {
            center.removeObserver(observer)
        }
        observers.removeAll()
    }

    deinit { stop() }
}
#endif
