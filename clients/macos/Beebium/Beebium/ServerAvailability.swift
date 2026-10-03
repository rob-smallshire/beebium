// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
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

/// Whether the server behind a window can currently be shown and driven, as
/// one value shared by everything in the window that presents server state.
///
/// It is resolved from the same two sources the emulator-area overlay uses --
/// `SystemClient.Liveness` and the `ReconnectCoordinator` phase -- with the
/// same precedence, so a sidebar can never contradict the overlay beside it.
enum ServerAvailability: Equatable {
    /// Connected and healthy: server state on screen is current.
    case live
    /// The server process ended unexpectedly. Terminal.
    case died
    /// The server was deliberately shut down. Terminal.
    case stopped
    /// Heartbeats stopped on a still-open connection. Recovers by itself if
    /// they resume.
    case unreachable
    /// The connection dropped and is being re-established.
    case reconnecting
    /// Automatic reconnection gave up; the user can retry.
    case reconnectFailed

    /// An active reconnect outranks the raw liveness: a dropped stream reads
    /// as `.died` even while the connection is already being brought back.
    static func resolve(liveness: SystemClient.Liveness,
                        reconnectPhase: ReconnectCoordinator.Phase) -> ServerAvailability {
        switch reconnectPhase {
        case .reconnecting:
            return .reconnecting
        case .givenUp:
            return .reconnectFailed
        case .idle:
            switch liveness {
            case .active: return .live
            case .died: return .died
            case .stopped: return .stopped
            case .unreachable: return .unreachable
            }
        }
    }

    var isLive: Bool { self == .live }

    /// Short statement of the state, matching the overlay's wording. Nil when
    /// live: there is nothing to say.
    var title: String? {
        switch self {
        case .live: return nil
        case .died: return "Emulator stopped unexpectedly"
        case .stopped: return "Emulator stopped"
        case .unreachable, .reconnecting: return "Connection lost"
        case .reconnectFailed: return "Couldn\u{2019}t reconnect"
        }
    }

    /// What the state means for the server state shown beneath the title.
    var detail: String? {
        switch self {
        case .live:
            return nil
        case .died, .stopped, .reconnectFailed:
            return "Showing the last known state. Controls are disabled."
        case .unreachable, .reconnecting:
            return "Showing the last known state. Controls are disabled until the connection returns."
        }
    }

    /// SF Symbol accompanying the title.
    var systemImage: String? {
        switch self {
        case .live: return nil
        case .died: return "exclamationmark.triangle.fill"
        case .stopped: return "stop.circle.fill"
        case .unreachable, .reconnecting: return "arrow.triangle.2.circlepath"
        case .reconnectFailed: return "wifi.exclamationmark"
        }
    }
}

/// How one sidebar mode presents itself for a given server availability.
///
/// A mode that shows or drives server state is marked stale when the server is
/// not live: a banner states why, and the content beneath is dimmed and
/// disabled so last-known values cannot be mistaken for current ones or acted
/// upon. A mode holding only this app's own settings is unaffected.
struct SidebarStalePresentation: Equatable {
    /// The availability to announce in a banner, or nil for no banner.
    let banner: ServerAvailability?
    /// Whether the mode's controls accept input.
    let isContentEnabled: Bool
    /// Whether the mode's content is drawn dimmed, as last-known state.
    let isContentDimmed: Bool

    init(mode: SidebarMode, availability: ServerAvailability) {
        let isStale = mode.showsServerState && !availability.isLive
        banner = isStale ? availability : nil
        isContentEnabled = !isStale
        isContentDimmed = isStale
    }
}

extension SidebarMode {
    /// Whether the mode shows or drives state held by the server, as opposed to
    /// settings that live in this app (display style, keyboard mapping).
    var showsServerState: Bool {
        switch self {
        case .storage, .memory, .peripherals, .sound, .processor, .network:
            return true
        case .video, .keyboard:
            return false
        }
    }
}

/// What the Network sidebar shows, derived from the server availability and
/// the cached Econet and transport state. All the decisions are here so the
/// view only lays out the result.
struct NetworkSidebarPresentation: Equatable {
    /// The cached state the presentation is derived from.
    struct Inputs: Equatable {
        var availability: ServerAvailability = .live
        /// An Econet status snapshot has been received on this connection.
        var econetIsLoaded: Bool = false
        /// The Econet status stream was lost; the snapshot is last-known.
        var econetIsStale: Bool = false
        var econetEnabled: Bool = false
        var econetConnected: Bool = false
        /// ListTransports has succeeded at least once on this connection.
        var transportsHaveListed: Bool = false
        /// A ListTransports attempt has completed, successfully or not.
        var transportsAttempted: Bool = false
        /// Ids of the listed transports that have a control panel.
        var transportPanelIDs: [String] = []
    }

    enum Content: Equatable {
        /// Waiting for the first status snapshot from a live server.
        case loading
        /// No snapshot was ever received and the server is not live, so there
        /// is nothing truthful to show about Econet at all.
        case unavailable
        /// The machine has no Econet hardware.
        case notFitted
        /// The status rows and transport area.
        case status
    }

    enum LinkState: Equatable {
        case connected
        case disconnected
        /// The link state cannot be known: the server is not live, or its
        /// status stream was lost.
        case unknown

        var label: String {
            switch self {
            case .connected: return "Connected"
            case .disconnected: return "Disconnected"
            case .unknown: return "Unknown"
            }
        }
    }

    enum TransportArea: Equatable {
        /// Nothing to show: the list is still loading, or the server reported
        /// no transport with a panel.
        case none
        /// The transports could not be listed. Distinct from `none` so a dead
        /// server never reads as a machine without a network transport.
        case unavailable
        /// Panels for these transport ids; dimmed and disabled when the server
        /// is not there to back them.
        case panels(ids: [String], isEnabled: Bool)
    }

    static let transportListUnavailableText = "Transport list unavailable"
    static let statusUnavailableText = "Econet status unavailable"

    let content: Content
    let linkState: LinkState
    /// The status rows are last-known values, not current ones.
    let isStatusStale: Bool
    let isStationEditorEnabled: Bool
    let transportArea: TransportArea

    init(_ inputs: Inputs) {
        let isLive = inputs.availability.isLive
        let isCurrent = isLive && !inputs.econetIsStale

        if !inputs.econetIsLoaded {
            content = isLive ? .loading : .unavailable
        } else if !inputs.econetEnabled {
            content = .notFitted
        } else {
            content = .status
        }

        if !isCurrent {
            linkState = .unknown
        } else {
            linkState = inputs.econetConnected ? .connected : .disconnected
        }
        isStatusStale = !isCurrent
        isStationEditorEnabled = isCurrent && content == .status

        if !inputs.transportsHaveListed {
            // Never listed: say so once there is a reason to believe the list
            // is not merely on its way.
            transportArea = (inputs.transportsAttempted || !isLive) ? .unavailable : .none
        } else if inputs.transportPanelIDs.isEmpty {
            transportArea = .none
        } else {
            transportArea = .panels(ids: inputs.transportPanelIDs, isEnabled: isCurrent)
        }
    }
}
