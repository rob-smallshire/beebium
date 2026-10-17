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
import GRPC

/// A failure to save a machine, mapped from the server's gRPC status to
/// something the UI can show. The save statuses are fixed (docs/grpc-server.md,
/// SaveMachine): a destination that already exists without Replace, a Replace of
/// something that is not a replaceable machine, a not-yet-saved machine with no
/// destination, and an internal failure.
enum MachineSaveError: Error, LocalizedError, Equatable {
    case notConnected
    case noDestination               // INVALID_ARGUMENT
    case destinationExists           // ALREADY_EXISTS
    case notReplaceable(String)      // FAILED_PRECONDITION, with the server's reason
    case failed(String)              // INTERNAL, or anything unexpected

    var errorDescription: String? {
        switch self {
        case .notConnected:            return "Not connected to the machine."
        case .noDestination:           return "Choose where to save this machine."
        case .destinationExists:       return "An item already exists at that location."
        case .notReplaceable(let why): return why.isEmpty ? "That item cannot be replaced." : why
        case .failed(let message):     return message
        }
    }
}

/// The Save surface over SystemService.SaveMachine: mapping the server's status
/// codes to a MachineSaveError, and composing the first-save panel's defaults.
/// The RPC call itself is on SystemClient, which owns the service channel; the
/// pure pieces live here so they are testable without a server.
enum SaveClient {

    /// Map a gRPC error from SaveMachine to a MachineSaveError. Pure.
    static func saveError(from error: Error) -> MachineSaveError {
        guard let status = error as? GRPCStatus else {
            return .failed(error.localizedDescription)
        }
        switch status.code {
        case .invalidArgument:    return .noDestination
        case .alreadyExists:      return .destinationExists
        case .failedPrecondition: return .notReplaceable(status.message ?? "")
        default:                  return .failed(status.message ?? "The machine could not be saved.")
        }
    }

    /// The first-save panel's starting folder and filename: the user's Documents
    /// folder, with no Beebium subfolder, and the machine's name (section 4.10).
    /// Pure.
    static func firstSaveDefaults(machineName: String, documentsDirpath: URL)
        -> (directoryURL: URL, nameFieldStringValue: String) {
        let trimmed = machineName.trimmingCharacters(in: .whitespacesAndNewlines)
        return (documentsDirpath, trimmed.isEmpty ? "Beebium Machine" : trimmed)
    }
}
