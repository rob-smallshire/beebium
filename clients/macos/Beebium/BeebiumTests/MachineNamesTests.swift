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

import XCTest
@testable import Beebium

@MainActor
final class MachineNameSequenceTests: XCTestCase {
    func testNamesArePresetPlusOrdinal() {
        let names = MachineNameSequence()

        XCTAssertEqual(names.next(forPreset: "BBC Model B"), "BBC Model B #1")
        XCTAssertEqual(names.next(forPreset: "BBC Model B"), "BBC Model B #2")
    }

    func testEachPresetCountsSeparately() {
        // Two machines from different presets are both the first of their
        // kind; sharing one counter would make the numbers say otherwise.
        let names = MachineNameSequence()

        XCTAssertEqual(names.next(forPreset: "BBC Model B"), "BBC Model B #1")
        XCTAssertEqual(names.next(forPreset: "BBC Model B (Disc)"), "BBC Model B (Disc) #1")
        XCTAssertEqual(names.next(forPreset: "BBC Model B"), "BBC Model B #2")
        XCTAssertEqual(names.next(forPreset: "BBC Model B (Disc)"), "BBC Model B (Disc) #2")
    }

    func testNumbersAreNeverReused() {
        // Nothing hands a number back: a name that has been on screen should
        // not reappear on a different machine later in the session.
        let names = MachineNameSequence()
        var seen: Set<String> = []

        for _ in 0..<50 {
            let name = names.next(forPreset: "BBC Model B")
            XCTAssertFalse(seen.contains(name), "reissued \(name)")
            seen.insert(name)
        }
        XCTAssertEqual(seen.count, 50)
    }

    func testANewSessionStartsAgainAtOne() {
        // Counters are per run of the app and not persisted, so a fresh
        // sequence -- as a relaunch creates -- begins at #1.
        XCTAssertEqual(MachineNameSequence().next(forPreset: "BBC Model B"),
                       "BBC Model B #1")
        XCTAssertEqual(MachineNameSequence().next(forPreset: "BBC Model B"),
                       "BBC Model B #1")
    }
}
