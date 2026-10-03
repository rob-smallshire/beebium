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
import GRPC

enum EconetError: LocalizedError {
    case notConnected
    case operationFailed(String)

    var errorDescription: String? {
        switch self {
        case .notConnected:
            return "Not connected to server"
        case .operationFailed(let message):
            return message
        }
    }
}

@MainActor
final class EconetClient: ObservableObject, Disconnectable {

    @Published private(set) var hasEconetSocket: Bool = false
    @Published private(set) var enabled: Bool = false
    @Published private(set) var stationId: UInt32 = 0
    @Published private(set) var aunMode: Bool = false
    @Published private(set) var connected: Bool = false
    /// The active transport requires real-time (1x) emulation (e.g. Piconet).
    @Published private(set) var requiresRealTime: Bool = false
    /// The transport is currently severed because the emulation speed is not 1x.
    @Published private(set) var gatedBySpeed: Bool = false
    /// AUN station-number collisions currently in effect on the network, and a
    /// description of the most recent one still in effect (empty when none) -- a
    /// transient gauge (#68/#138): two stations claim the same number and the
    /// first live one keeps it. The server clears these once the collisions end.
    @Published private(set) var stationCollisionCount: UInt32 = 0
    @Published private(set) var lastStationCollision: String = ""
    @Published private(set) var isLoaded: Bool = false
    /// The status stream ended for a reason other than our own cancellation, so
    /// the published status is the last the server reported, not its current
    /// state. The live gauges (`connected`, `gatedBySpeed`, the collision
    /// fields) are cleared when this is set; the configuration (`enabled`,
    /// `stationId`, `aunMode`) is kept as last-known for display. Cleared by
    /// the next snapshot from a fresh stream, or by `disconnect()`.
    @Published private(set) var isStale: Bool = false
    @Published private(set) var errorMessage: String?

    /// How a status stream ended, reduced to what the client state depends on.
    enum StreamEnd: Equatable {
        /// We cancelled it (disconnect / reconnect), or the server closed it OK.
        case finished
        /// The stream was lost: the server is gone or unreachable.
        case lost(String)
    }

    private var client: Beebium_EconetServiceNIOClient?
    private var statusStreamCall: ServerStreamingCall<Beebium_WatchEconetStatusRequest, Beebium_GetEconetStatusResponse>?

    /// Identifies the current status stream. Bumped whenever a stream is started
    /// or abandoned, so the completion of a superseded stream -- which can
    /// arrive after a reconnect has already started its successor -- cannot
    /// mark the new stream's status stale.
    private(set) var streamGeneration: UInt64 = 0

    func connect(channel: GRPCChannel) {
        client = Beebium_EconetServiceNIOClient(channel: channel)
        startStatusStream()
    }

    func disconnect() {
        streamGeneration &+= 1
        statusStreamCall?.cancel(promise: nil)
        statusStreamCall = nil
        client = nil
        isLoaded = false
        isStale = false
        hasEconetSocket = false
        enabled = false
        stationId = 0
        aunMode = false
        connected = false
        requiresRealTime = false
        gatedBySpeed = false
        stationCollisionCount = 0
        lastStationCollision = ""
        errorMessage = nil
    }

    // MARK: - Mutations

    func setStationId(_ newId: UInt32) async -> Result<Void, EconetError> {
        guard let client = client else {
            NSLog("[EconetClient] SetStationId refused: not connected")
            return .failure(.notConnected)
        }
        guard !isStale else {
            NSLog("[EconetClient] SetStationId refused: status stream lost, server presumed gone")
            return .failure(.notConnected)
        }
        var request = Beebium_SetStationIdRequest()
        request.stationID = newId
        do {
            let response = try await client.setStationId(request).response.get()
            if response.success {
                return .success(())
            } else {
                return .failure(.operationFailed(response.error))
            }
        } catch {
            return .failure(.operationFailed(error.localizedDescription))
        }
    }

    // MARK: - Private

    // Subscribe to the server's WatchEconetStatus stream. The server
    // pushes an initial snapshot on subscription, then a fresh snapshot
    // whenever status visible on EconetService changes (enable/disable,
    // station id, or transport backend connection toggle).
    private func startStatusStream() {
        guard let client = client else {
            NSLog("[EconetClient] Status stream not started: not connected")
            return
        }

        streamGeneration &+= 1
        let generation = streamGeneration

        let request = Beebium_WatchEconetStatusRequest()
        let call = client.watchEconetStatus(request) { [weak self] status in
            Task { @MainActor [weak self] in
                self?.handleStatusUpdate(status, generation: generation)
            }
        }

        statusStreamCall = call

        // A server-streaming call's status future succeeds with a non-OK code
        // when the server goes away; it only fails for local errors. Both are a
        // loss unless the code says we cancelled the stream ourselves.
        call.status.whenComplete { [weak self] result in
            let end: StreamEnd
            switch result {
            case .success(let status):
                end = (status.code == .ok || status.code == .cancelled)
                    ? .finished
                    : .lost(status.description)
            case .failure(let error):
                end = .lost(error.localizedDescription)
            }
            Task { @MainActor [weak self] in
                self?.handleStreamEnd(end, generation: generation)
            }
        }
    }

    /// Apply the end of the status stream identified by `generation`. A lost
    /// stream leaves the last-known configuration in place but marks it stale
    /// and clears the live gauges, so nothing reads as a live link to a server
    /// that is gone. The end of a superseded stream is ignored.
    func handleStreamEnd(_ end: StreamEnd, generation: UInt64) {
        guard generation == streamGeneration else {
            NSLog("[EconetClient] Ignoring end of superseded status stream")
            return
        }
        statusStreamCall = nil
        switch end {
        case .finished:
            break
        case .lost(let reason):
            NSLog("[EconetClient] Status stream lost: %@", reason)
            isStale = true
            connected = false
            gatedBySpeed = false
            stationCollisionCount = 0
            lastStationCollision = ""
            errorMessage = "Econet status stream ended: \(reason)"
        }
    }

    /// Apply a status snapshot from the stream identified by `generation`. A
    /// snapshot from the current stream is by definition fresh, so it clears
    /// staleness; one from a superseded stream is ignored.
    func handleStatusUpdate(_ response: Beebium_GetEconetStatusResponse, generation: UInt64) {
        guard generation == streamGeneration else {
            NSLog("[EconetClient] Ignoring status from superseded status stream")
            return
        }
        hasEconetSocket = response.hasEconetSocket_p
        enabled = response.enabled
        stationId = response.stationID
        aunMode = response.aunMode
        connected = response.connected
        requiresRealTime = response.requiresRealTime
        gatedBySpeed = response.gatedBySpeed
        stationCollisionCount = response.aunStationCollisionCount
        lastStationCollision = response.aunLastStationCollision
        isLoaded = true
        isStale = false
        errorMessage = nil
    }
}
