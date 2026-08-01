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

/// Names for machines the app starts, so that several windows can be told
/// apart at a glance.
///
/// A machine's name is per instance, not part of a preset: two machines built
/// from one preset are still two machines. The server will happily default the
/// name to the model's display name, but then every Model B is called "BBC
/// Model B 32K" and the windows are indistinguishable -- which is the problem
/// this exists to solve. So a name is chosen at launch rather than left to
/// that default, and the user can rename it afterwards.
///
/// Cambridge colleges, for a machine designed in Cambridge -- and following
/// Acorn's own habit, which named the RISC OS outline fonts Trinity, Corpus
/// and Homerton. Proper names are distinct at a glance and easy to say out
/// loud, in a way that "Model B 2" and "Model B 3" are not.
enum MachineNames {
    /// Single words, so a title bar reads cleanly: the distinguishing part of
    /// each college's name, with "St" dropped. Clare Hall and Trinity Hall
    /// have no distinguishing word of their own -- shortening them would
    /// duplicate Clare and Trinity -- so they are left out rather than
    /// admitted as two more names that look like ones already here.
    static let pool: [String] = [
        "Caius",
        "Catharine's",
        "Cavendish",
        "Christ's",
        "Churchill",
        "Clare",
        "Corpus",
        "Darwin",
        "Downing",
        "Edmund's",
        "Edwards",
        "Emmanuel",
        "Fitzwilliam",
        "Girton",
        "Homerton",
        "Hughes",
        "Jesus",
        "John's",
        "King's",
        "Magdalene",
        "Newnham",
        "Pembroke",
        "Peterhouse",
        "Queens'",
        "Robinson",
        "Selwyn",
        "Sussex",
        "Trinity",
        "Wolfson",
    ]

    /// A name not among those already taken.
    ///
    /// Picked at random from whatever is left, rather than in order, so that
    /// two machines started one after the other do not get names that look
    /// alike. Once the pool is used up, names repeat with a numeric suffix --
    /// "Trinity 2" and so on -- which is a worse name but still a distinct
    /// one, and only reachable after twenty-nine machines in one session.
    static func next(avoiding taken: Set<String>) -> String {
        let free = pool.filter { !taken.contains($0) }
        if let name = free.randomElement() {
            return name
        }

        // Every name is in use. Find the lowest suffix that is not.
        for suffix in 2... {
            let candidates = pool.map { "\($0) \(suffix)" }.filter { !taken.contains($0) }
            if let name = candidates.randomElement() {
                return name
            }
        }
        // Unreachable: the loop above only ends by returning.
        return pool[0]
    }
}
