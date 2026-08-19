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

/// What the server's `describe-disc-image` subcommand reports about a file.
///
/// The judgement is the server's, made by the same format detection it applies
/// when a disc is inserted, so the client never has to encode its own rule for
/// what a disc image is. The size/write fields are present only when the image
/// was recognised; `reason` only when it was not.
struct DiscImageInfo: Codable {
    let recognised: Bool
    let format: String
    let sides: Int?
    let writeProtected: Bool?
    let reason: String?

    enum CodingKeys: String, CodingKey {
        case recognised
        case format
        case sides
        case writeProtected = "write_protected"
        case reason
    }
}
