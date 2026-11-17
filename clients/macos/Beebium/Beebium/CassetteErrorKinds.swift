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

extension Beebium_CassetteErrorKind {
    /// The short, honest inline marker label for this failure kind, or nil for
    /// an unclassified failure -- in which case the caller keeps its own
    /// context-appropriate generic ("Insert failed", "Play failed", ...), so a
    /// future server kind we don't yet map still reads honestly. The popover
    /// always shows the server's verbatim sentence regardless.
    var markerLabel: String? {
        switch self {
        case .noCassetteInterface: return "No cassette interface"
        case .cannotOpen: return "Cannot open"
        case .unreadable: return "Unreadable tape"
        case .unsupportedFormat: return "Unsupported format"
        case .invalidImage: return "Invalid tape image"
        case .deckOccupied: return "Deck occupied"
        case .deckEmpty: return "Deck empty"
        case .invalidPosition: return "Invalid position"
        case .readOnly: return "Read-only tape"
        case .liveSource: return "Not on a live tape"
        case .invalidFormat: return "Invalid audio format"
        case .unspecified, .UNRECOGNIZED: return nil
        }
    }
}

extension Beebium_CreateTapeErrorKind {
    /// The short, honest inline marker label for a failed tape creation, or nil
    /// for an unclassified failure -- in which case the caller keeps its own
    /// generic ("Couldn't create tape"), so a future server kind we don't yet
    /// map still reads honestly. The popover shows the full sentence regardless.
    var markerLabel: String? {
        switch self {
        case .unknownLabel: return "Unknown template"
        case .destinationExists: return "File exists"
        case .writeFailed: return "Couldn't write tape"
        case .wrongExtension: return "Wrong extension"
        case .unspecified, .UNRECOGNIZED: return nil
        }
    }

    /// The capitalised lead for the user-facing sentence a failed creation
    /// shows: an alert must be a sentence, while the server's own message is a
    /// lower-case fragment, so this leads and the fragment completes it (via
    /// SaveClient.sentence).
    var sentenceLead: String {
        switch self {
        case .unknownLabel: return "That blank tape template is not available."
        case .destinationExists: return "A file already exists there."
        case .writeFailed: return "The tape image could not be written."
        case .wrongExtension: return "That name must end in .uef for a tape."
        case .unspecified, .UNRECOGNIZED: return "The tape image could not be created."
        }
    }
}
