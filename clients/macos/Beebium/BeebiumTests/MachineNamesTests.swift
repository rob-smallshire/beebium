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

    func testHonoursAcornsFontNaming() {
        // Acorn named the RISC OS outline fonts after Cambridge colleges;
        // these three are the ones everyone remembers, so losing them would
        // lose the reference.
        for name in ["Trinity", "Corpus", "Homerton"] {
            XCTAssertTrue(MachineNames.pool.contains(name), "\(name) missing")
        }
    }
}


final class MachineNameAllocatorTests: XCTestCase {
    func testIssuesEveryNameBeforeRepeatingAny() {
        let allocator = MachineNameAllocator()
        var issued: [String] = []
        for _ in MachineNames.pool.indices {
            issued.append(allocator.issue())
        }
        XCTAssertEqual(Set(issued), Set(MachineNames.pool))
    }

    func testOrderIsShuffled() {
        // The first machine of a session should not always be the same
        // college. Two allocators agreeing throughout would mean a fixed
        // order; across several the chance of coincidence is negligible.
        let orders = (0..<8).map { _ -> [String] in
            let allocator = MachineNameAllocator()
            return MachineNames.pool.indices.map { _ in allocator.issue() }
        }
        XCTAssertGreaterThan(Set(orders).count, 1, "issue order looks fixed")
    }

    func testAReleasedNameGoesToTheBack() {
        // A name freed by closing a window must be the last to come round
        // again, so a new machine is not handed the name just vacated.
        let allocator = MachineNameAllocator(pool: ["A", "B", "C"])
        let first = allocator.issue()
        allocator.release(first)

        XCTAssertNotEqual(allocator.issue(), first)
        XCTAssertNotEqual(allocator.issue(), first)
        XCTAssertEqual(allocator.issue(), first, "released name should come last")
    }

    func testReleasingTwiceDoesNotDuplicate() {
        let allocator = MachineNameAllocator(pool: ["A", "B"])
        let name = allocator.issue()
        allocator.release(name)
        allocator.release(name)

        XCTAssertEqual(allocator.freeCount, 2)
    }

    func testIgnoresNamesItNeverMinted() {
        // The user may call a machine anything; that must not put a foreign
        // name into the pool.
        let allocator = MachineNameAllocator(pool: ["A", "B"])
        allocator.release("Fred")

        XCTAssertEqual(allocator.freeCount, 2)
        XCTAssertFalse([allocator.issue(), allocator.issue()].contains("Fred"))
    }

    func testClaimTakesANameOutOfCirculation() {
        // A machine renamed by hand onto a pool name must not be duplicated
        // by a later generated one.
        let allocator = MachineNameAllocator(pool: ["A", "B", "C"])
        allocator.claim("B")

        let issued = [allocator.issue(), allocator.issue()]
        XCTAssertFalse(issued.contains("B"))
        XCTAssertEqual(Set(issued), ["A", "C"])
    }

    func testClaimingAnUnknownNameChangesNothing() {
        let allocator = MachineNameAllocator(pool: ["A", "B"])
        allocator.claim("Fred")
        XCTAssertEqual(allocator.freeCount, 2)
    }

    func testExhaustionFallsBackToSuffixes() {
        let allocator = MachineNameAllocator(pool: ["A", "B"])
        _ = allocator.issue()
        _ = allocator.issue()

        let overflow = [allocator.issue(), allocator.issue()]
        XCTAssertEqual(Set(overflow), ["A 2", "B 2"])
    }

    func testSuffixesKeepClimbing() {
        let allocator = MachineNameAllocator(pool: ["A"])
        XCTAssertEqual(allocator.issue(), "A")
        XCTAssertEqual(allocator.issue(), "A 2")
        XCTAssertEqual(allocator.issue(), "A 3")
    }

    func testSuffixedNamesAreReusableToo() {
        let allocator = MachineNameAllocator(pool: ["A"])
        _ = allocator.issue()
        let second = allocator.issue()
        allocator.release(second)

        XCTAssertEqual(allocator.issue(), second)
    }

    func testAvoidsNamesSeenElsewhere() {
        let allocator = MachineNameAllocator(pool: ["A", "B", "C"])

        XCTAssertEqual(allocator.issue(avoiding: ["A", "B"]), "C")
    }

    func testAvoidedNamesStayAvailable() {
        // A name in use on the network is skipped, not consumed: the machine
        // holding it may well be gone by the time we look again.
        let allocator = MachineNameAllocator(pool: ["A", "B"])

        XCTAssertEqual(allocator.issue(avoiding: ["A"]), "B")
        XCTAssertEqual(allocator.issue(avoiding: []), "A")
    }

    func testExtendsRatherThanDuplicatingWhenEverythingIsTaken() {
        let allocator = MachineNameAllocator(pool: ["A", "B"])

        let name = allocator.issue(avoiding: ["A", "B"])

        XCTAssertEqual(Set(["A 2", "B 2"]).contains(name), true, "got \(name)")
    }

    func testAcceptsADuplicateRatherThanFailingToName() {
        // If even the extended vocabulary is spoken for, a duplicate name is
        // the lesser evil: a machine with an awkward name beats no machine.
        let allocator = MachineNameAllocator(pool: ["A"])
        let everything: Set<String> = ["A", "A 2", "A 3", "A 4", "A 5"]

        let name = allocator.issue(avoiding: everything)

        XCTAssertFalse(name.isEmpty)
    }

    func testAnEmptyAvoidSetChangesNothing() {
        // What happens when Bonjour is unavailable: naming carries on.
        let allocator = MachineNameAllocator(pool: ["A", "B"])

        XCTAssertEqual(Set([allocator.issue(avoiding: []), allocator.issue(avoiding: [])]),
                       ["A", "B"])
    }

    func testNeverIssuesTheSameNameTwiceWhileInUse() {
        // The property that matters, across a long run of churn.
        let allocator = MachineNameAllocator(pool: ["A", "B", "C"])
        var inUse: Set<String> = []
        for step in 0..<200 {
            if step % 3 == 2, let victim = inUse.randomElement() {
                inUse.remove(victim)
                allocator.release(victim)
            } else {
                let name = allocator.issue()
                XCTAssertFalse(inUse.contains(name), "reissued \(name) while in use")
                inUse.insert(name)
            }
        }
    }
}
