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

import Foundation
import GRPC

/// Error type for disc operations
enum DiscError: LocalizedError {
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

/// Client for managing disc drives via gRPC
@MainActor
final class DiscClient: ObservableObject, Disconnectable {
    /// Status of all drives
    @Published private(set) var drives: [Beebium_DriveStatus] = []

    /// Whether the machine has a disc controller
    @Published private(set) var hasDiscController: Bool = false

    /// Disc controller type (e.g., "WD1770", "8271")
    @Published private(set) var controllerType: String = ""

    /// Whether drive status has been successfully loaded
    @Published private(set) var isLoaded: Bool = false

    /// Error message if loading or operations failed
    @Published private(set) var errorMessage: String?

    private var client: Beebium_DiscServiceNIOClient?
    private var subscriptionTask: Task<Void, Never>?

    /// Connect to the server using an existing gRPC channel
    func connect(channel: GRPCChannel) {
        client = Beebium_DiscServiceNIOClient(channel: channel)

        subscriptionTask = Task { [weak self] in
            await self?.fetchStatusAndSubscribe()
        }
    }

    /// Disconnect from the server
    func disconnect() {
        subscriptionTask?.cancel()
        subscriptionTask = nil
        client = nil
        isLoaded = false
        drives = []
        hasDiscController = false
        controllerType = ""
        errorMessage = nil
    }

    /// Insert a disc image into the specified drive
    /// - Parameters:
    ///   - drive: Drive number (0 or 1)
    ///   - url: File URL of the disc image
    /// - Returns: Result with disc metadata on success or error on failure
    func insertDisc(drive: Int, url: URL) async -> Result<Beebium_DiscMetadata, DiscError> {
        guard let client = client else {
            return .failure(.notConnected)
        }

        var request = Beebium_InsertDiscRequest()
        request.drive = UInt32(drive)
        request.url = url.absoluteString

        do {
            let response = try await client.insertDisc(request).response.get()
            if response.success {
                return .success(response.disc)
            } else {
                return .failure(.operationFailed(response.error))
            }
        } catch {
            return .failure(.operationFailed(error.localizedDescription))
        }
    }

    /// Eject a disc from the specified drive
    /// - Parameters:
    ///   - drive: Drive number (0 or 1)
    ///   - immediate: If true, eject immediately without waiting for motor to stop
    /// - Returns: Result indicating success or error
    func ejectDisc(drive: Int, immediate: Bool = false) async -> Result<Void, DiscError> {
        guard let client = client else {
            return .failure(.notConnected)
        }

        var request = Beebium_EjectDiscRequest()
        request.drive = UInt32(drive)
        request.immediate = immediate

        do {
            let response = try await client.ejectDisc(request).response.get()
            if response.accepted {
                return .success(())
            } else {
                return .failure(.operationFailed(response.error))
            }
        } catch {
            return .failure(.operationFailed(error.localizedDescription))
        }
    }

    /// Fetch drive status and subscribe to events
    private func fetchStatusAndSubscribe() async {
        guard let client = client else { return }

        // First, fetch current drive status
        do {
            let request = Beebium_GetDriveStatusRequest()
            let response = try await client.getDriveStatus(request).response.get()

            await MainActor.run {
                self.hasDiscController = response.hasDiscController_p
                self.controllerType = response.controllerType
                self.drives = response.drives
                self.isLoaded = true
                self.errorMessage = nil
            }
        } catch {
            await MainActor.run {
                self.errorMessage = "Failed to get drive status: \(error.localizedDescription)"
                self.isLoaded = false
            }
            return
        }

        // Then subscribe to streaming events
        let request = Beebium_SubscribeDiscEventsRequest()

        let call = client.subscribeDiscEvents(request) { [weak self] event in
            Task { @MainActor [weak self] in
                self?.handleEvent(event)
            }
        }

        // Wait for stream to complete or be cancelled
        do {
            _ = try await call.status.get()
        } catch {
            await MainActor.run {
                if self.isLoaded {
                    self.errorMessage = "Disc event stream ended: \(error.localizedDescription)"
                }
            }
        }
    }

    /// Handle incoming disc events
    private func handleEvent(_ event: Beebium_DiscEvent) {
        let driveIndex = Int(event.drive)

        switch event.type {
        case .discEventInserted:
            // Update drive state to loaded
            if driveIndex < drives.count {
                drives[driveIndex].state = .loaded
                drives[driveIndex].discURL = event.discURL
                if event.hasDisc {
                    drives[driveIndex].disc = event.disc
                    drives[driveIndex].discName = event.disc.name
                    drives[driveIndex].writeProtected = event.disc.writeProtected
                }
            }

        case .discEventEjected, .discEventForceEjected:
            // Update drive state to empty
            if driveIndex < drives.count {
                drives[driveIndex].state = .empty
                drives[driveIndex].discName = ""
                drives[driveIndex].discURL = ""
                drives[driveIndex].writeProtected = false
                drives[driveIndex].clearDisc()
            }

        case .discEventEjectRequested:
            // Update drive state to ejecting
            if driveIndex < drives.count {
                drives[driveIndex].state = .ejecting
            }

        case .discEventEjectCancelled:
            // Revert to loaded state (eject was cancelled)
            if driveIndex < drives.count {
                drives[driveIndex].state = .loaded
            }

        case .discEventMotorOn:
            if driveIndex < drives.count {
                drives[driveIndex].motorOn = true
            }

        case .discEventMotorOff:
            if driveIndex < drives.count {
                drives[driveIndex].motorOn = false
            }

        case .discEventUnknown, .UNRECOGNIZED:
            break
        }
    }

    /// Refresh drive status from the server
    func refreshStatus() async {
        guard let client = client else { return }

        do {
            let request = Beebium_GetDriveStatusRequest()
            let response = try await client.getDriveStatus(request).response.get()

            await MainActor.run {
                self.hasDiscController = response.hasDiscController_p
                self.controllerType = response.controllerType
                self.drives = response.drives
                self.errorMessage = nil
            }
        } catch {
            await MainActor.run {
                self.errorMessage = "Failed to refresh: \(error.localizedDescription)"
            }
        }
    }
}
