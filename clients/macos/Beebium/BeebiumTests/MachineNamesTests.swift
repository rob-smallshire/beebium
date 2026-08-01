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

final class MachineNamesTests: XCTestCase {
    func testNamesAreDistinct() {
        XCTAssertEqual(Set(MachineNames.pool).count, MachineNames.pool.count)
    }

    func testNamesAreNotEmpty() {
        XCTAssertFalse(MachineNames.pool.isEmpty)
        XCTAssertTrue(MachineNames.pool.allSatisfy { !$0.isEmpty })
    }

    func testPicksFromThePoolWhenNothingIsTaken() {
        XCTAssertTrue(MachineNames.pool.contains(MachineNames.next(avoiding: [])))
    }

    func testNeverPicksATakenName() {
        // The whole point: a second machine must not arrive wearing the name
        // of one already on screen.
        var taken: Set<String> = []
        for _ in MachineNames.pool.indices {
            let name = MachineNames.next(avoiding: taken)
            XCTAssertFalse(taken.contains(name), "reissued \(name)")
            taken.insert(name)
        }
        XCTAssertEqual(taken.count, MachineNames.pool.count)
    }

    func testExhaustingThePoolFallsBackToSuffixes() {
        let taken = Set(MachineNames.pool)
        let name = MachineNames.next(avoiding: taken)

        XCTAssertFalse(taken.contains(name))
        XCTAssertTrue(name.hasSuffix(" 2"), "expected a suffixed name, got \(name)")
        XCTAssertTrue(MachineNames.pool.contains(String(name.dropLast(2))))
    }

    func testSuffixesKeepClimbingWhileNamesAreTaken() {
        var taken = Set(MachineNames.pool)
        taken.formUnion(MachineNames.pool.map { "\($0) 2" })

        let name = MachineNames.next(avoiding: taken)

        XCTAssertFalse(taken.contains(name))
        XCTAssertTrue(name.hasSuffix(" 3"), "expected a 3-suffixed name, got \(name)")
    }

    func testHonoursAcornsFontNaming() {
        // Acorn named the RISC OS outline fonts after Cambridge colleges;
        // these three are the ones everyone remembers, so losing them would
        // lose the reference.
        for name in ["Trinity", "Corpus", "Homerton"] {
            XCTAssertTrue(MachineNames.pool.contains(name), "\(name) missing")
        }
    }
}
