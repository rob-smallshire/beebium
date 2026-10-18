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

/// Error types for debugger client operations
enum DebuggerClientError: Error, LocalizedError {
    case notConnected

    var errorDescription: String? {
        switch self {
        case .notConnected:
            return "Debugger client not connected"
        }
    }
}

/// Client for debugger control operations via gRPC.
/// Provides Run() RPC to start emulation for cores launched with --wait=api.
@MainActor
final class DebuggerClient: ObservableObject, Disconnectable {
    private var client: Beebium_DebuggerControlNIOClient?

    /// Connect to the server using an existing gRPC channel
    func connect(channel: GRPCChannel) {
        client = Beebium_DebuggerControlNIOClient(channel: channel)
    }

    /// Disconnect from the server
    func disconnect() {
        client = nil
    }

    /// Start emulation (for cores launched with --wait=api).
    /// This unblocks the emulator and allows it to begin executing.
    func run() async throws {
        guard let client = client else {
            throw DebuggerClientError.notConnected
        }
        // A default RunRequest (stop_at_current = false) resumes execution the
        // way the old parameterless Run did: step over any breakpoint at the
        // current instruction rather than stopping on it again.
        _ = try await client.run(Beebium_RunRequest()).response.get()
    }
}
