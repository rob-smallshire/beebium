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

/// Client for PeripheralExtensionService.ListExtensions.
///
/// Fetches the configured peripheral extensions once when the channel
/// comes up and exposes them as a hierarchical `PeripheralTree`.
/// Extensions are configured at server start and don't change at
/// runtime, so a one-shot fetch is sufficient; per-extension state
/// (LEDs, status, etc.) is delivered by the ExtensionUiService stream
/// for any peripheral that implements its own ExtensionUi.
@MainActor
final class PeripheralsClient: ObservableObject, Disconnectable {

    @Published private(set) var tree: PeripheralTree =
        PeripheralTree(groups: [], orphans: [], pointDisplayNames: [:])
    @Published private(set) var isLoaded: Bool = false
    @Published private(set) var errorMessage: String?

    private var client: Beebium_PeripheralExtensionServiceNIOClient?

    func connect(channel: GRPCChannel) {
        client = Beebium_PeripheralExtensionServiceNIOClient(channel: channel)
        Task { @MainActor in
            await refresh()
        }
    }

    func disconnect() {
        client = nil
        tree = PeripheralTree(groups: [], orphans: [], pointDisplayNames: [:])
        isLoaded = false
        errorMessage = nil
    }

    func refresh() async {
        guard let client = client else { return }
        let request = Beebium_ListExtensionsRequest()
        do {
            let response = try await client.listExtensions(request).response.get()
            self.tree = PeripheralTree.build(from: response.extensions,
                                             attachmentPoints: response.attachmentPoints)
            self.isLoaded = true
            self.errorMessage = nil
        } catch {
            NSLog("[PeripheralsClient] ListExtensions error: %@",
                  error.localizedDescription)
            self.errorMessage = error.localizedDescription
            // isLoaded remains as-is: if this is the first attempt, the
            // sidebar shows the error path; if a previous refresh
            // succeeded we keep the tree visible.
            self.isLoaded = true
        }
    }
}
