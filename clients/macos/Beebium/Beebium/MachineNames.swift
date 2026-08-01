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

}

/// Hands out machine names, and takes them back.
///
/// A shuffled circular queue: names are issued from the front and returned to
/// the back. Shuffling means the first machine of a session is not always the
/// same college, and returning to the back means a name freed by a closed
/// window is the last to be reused rather than the first -- so a new machine
/// does not arrive wearing the name of the one you just closed.
///
/// When the queue runs dry the vocabulary is extended with the next numeric
/// suffix -- "Trinity 2" and so on -- shuffled in its turn. Names are always
/// distinct; they only ever get uglier.
final class MachineNameAllocator {
    private var free: [String]
    /// Every name this allocator has ever minted, so that a name handed back
    /// can be told apart from one the user typed. Returning "Fred" to the
    /// queue would put a name in the pool that was never ours to give.
    private var minted: Set<String>
    private let pool: [String]
    private var suffix = 1

    init(pool: [String] = MachineNames.pool) {
        self.pool = pool
        self.free = pool.shuffled()
        self.minted = Set(pool)
    }

    /// Take the next name.
    func issue() -> String {
        if free.isEmpty {
            extendVocabulary()
        }
        return free.removeFirst()
    }

    /// Mark a specific name as in use, if it is one of ours.
    ///
    /// For a machine renamed by hand onto a name we might otherwise have
    /// issued. A name that was never ours is ignored: the user is free to
    /// call a machine anything, and it does not consume the pool.
    func claim(_ name: String) {
        free.removeAll { $0 == name }
    }

    /// Give a name back, for reuse after everything else.
    ///
    /// Ignores names this allocator never minted, and names already free, so
    /// releasing twice cannot put a duplicate in the queue.
    func release(_ name: String) {
        guard minted.contains(name), !free.contains(name) else { return }
        free.append(name)
    }

    /// Names still available, for tests.
    var freeCount: Int { free.count }

    private func extendVocabulary() {
        suffix += 1
        let suffixed = pool.map { "\($0) \(suffix)" }
        minted.formUnion(suffixed)
        free = suffixed.shuffled()
    }

}
