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

/// One discovered Econet transport, flattened from the proto message.
///
/// `id` is the opaque, server-assigned key the Network sidebar passes to
/// ExtensionUiService to drive this transport's control panel -- never a
/// hardcoded extension name. `hasUI` says whether a panel exists at all.
struct EconetTransportInfo: Identifiable, Hashable {
    let id: String
    let name: String
    let description: String
    let active: Bool
    let hasUI: Bool
}

/// Client for EconetTransportService.ListTransports.
///
/// Mirrors `PeripheralsClient`: transports are configured at server
/// start and don't change at runtime, so a one-shot fetch when the
/// channel comes up is sufficient. The Network sidebar uses the
/// discovered `id`/`hasUI` to render an ExtensionUiService panel per
/// transport, so any new transport extension surfaces with no client
/// changes -- the client holds no knowledge of specific transport types.
@MainActor
final class EconetTransportsClient: ObservableObject, Disconnectable {

    @Published private(set) var transports: [EconetTransportInfo] = []
    /// A ListTransports attempt has completed (successfully or not).
    @Published private(set) var isLoaded: Bool = false
    /// ListTransports has succeeded at least once on this connection, so
    /// `transports` is what the server reported (possibly empty) rather than
    /// the initial placeholder. False with `isLoaded` true means the list is
    /// unavailable, which is not the same as there being no transports.
    @Published private(set) var hasListed: Bool = false
    @Published private(set) var errorMessage: String?

    private var client: Beebium_EconetTransportServiceNIOClient?

    func connect(channel: GRPCChannel) {
        client = Beebium_EconetTransportServiceNIOClient(channel: channel)
        Task { @MainActor in
            await refresh()
        }
    }

    func disconnect() {
        client = nil
        transports = []
        isLoaded = false
        hasListed = false
        errorMessage = nil
    }

    func refresh() async {
        guard let client = client else {
            NSLog("[EconetTransportsClient] ListTransports skipped: not connected")
            return
        }
        let request = Beebium_ListTransportsRequest()
        do {
            let response = try await client.listTransports(request).response.get()
            applyListResult(.success(response.transports.map { t in
                EconetTransportInfo(id: t.id,
                                    name: t.name,
                                    description: t.description_p,
                                    active: t.active,
                                    hasUI: t.hasUi_p)
            }))
        } catch {
            applyListResult(.failure(error))
        }
    }

    /// Apply the outcome of a ListTransports attempt. A failure keeps the last
    /// successfully listed transports (the configuration does not change at
    /// runtime, so they remain the best description of the machine) and leaves
    /// `hasListed` alone, so a first attempt that fails is distinguishable from
    /// a server that reported no transports.
    func applyListResult(_ result: Result<[EconetTransportInfo], Error>) {
        switch result {
        case .success(let listed):
            transports = listed
            hasListed = true
            errorMessage = nil
        case .failure(let error):
            NSLog("[EconetTransportsClient] ListTransports error: %@",
                  error.localizedDescription)
            errorMessage = error.localizedDescription
        }
        isLoaded = true
    }
}
