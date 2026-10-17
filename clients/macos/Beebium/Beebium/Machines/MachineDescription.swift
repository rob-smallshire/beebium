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

/// One saved machine as `describe-machine --format jsonl` reports it. The Open
/// flow and the Welcome window's Machines page read this rather than parsing
/// `machine.json`, so the server stays the single source of a machine's
/// identity.
///
/// The optional fields are genuinely absent in the JSON (never null): `preset`
/// when the source preset is unknown, `lastUsed` until the machine has been run
/// once, `thumbnailFilepath` when there is no thumbnail, and `lockedByPid` when
/// no server holds the directory's lock.
struct MachineDescription: Decodable, Equatable, Identifiable {
    let machineDirpath: String
    let name: String
    let uuid: String
    let model: String
    let preset: String?
    let created: String
    let lastUsed: String?
    let thumbnailFilepath: String?
    let lockedByPid: Int?

    /// Stable across opens (it is the machine.json UUID), so it identifies a row
    /// in the Machines page and a running machine on the status stream.
    var id: String { uuid }

    /// Whether a server currently holds the directory's lock.
    var isRunning: Bool { lockedByPid != nil }

    enum CodingKeys: String, CodingKey {
        case machineDirpath = "machine_dirpath"
        case name, uuid, model, preset, created
        case lastUsed = "last_used"
        case thumbnailFilepath = "thumbnail_filepath"
        case lockedByPid = "locked_by_pid"
    }
}
