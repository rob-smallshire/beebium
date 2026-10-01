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
    @Published private(set) var errorMessage: String?

    private var client: Beebium_EconetServiceNIOClient?
    private var statusStreamCall: ServerStreamingCall<Beebium_WatchEconetStatusRequest, Beebium_GetEconetStatusResponse>?

    func connect(channel: GRPCChannel) {
        client = Beebium_EconetServiceNIOClient(channel: channel)
        startStatusStream()
    }

    func disconnect() {
        statusStreamCall?.cancel(promise: nil)
        statusStreamCall = nil
        client = nil
        isLoaded = false
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
        guard let client = client else { return .failure(.notConnected) }
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
    // station id, or transport backend connection toggle). Replaces the
    // earlier 500ms polling workaround (project_econet_status_streaming.md).
    private func startStatusStream() {
        guard let client = client else { return }

        let request = Beebium_WatchEconetStatusRequest()
        let call = client.watchEconetStatus(request) { [weak self] status in
            Task { @MainActor [weak self] in
                self?.handleStatusUpdate(status)
            }
        }

        statusStreamCall = call

        call.status.whenComplete { [weak self] result in
            Task { @MainActor [weak self] in
                switch result {
                case .success(let status):
                    if status.code != .ok && status.code != .cancelled {
                        NSLog("[EconetClient] Status stream ended: %@", status.description)
                        self?.errorMessage = "Econet status stream ended: \(status.description)"
                    }
                case .failure(let error):
                    NSLog("[EconetClient] Status stream error: %@", error.localizedDescription)
                    self?.errorMessage = "Econet status stream error: \(error.localizedDescription)"
                }
                self?.statusStreamCall = nil
            }
        }
    }

    private func handleStatusUpdate(_ response: Beebium_GetEconetStatusResponse) {
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
        errorMessage = nil
    }
}
