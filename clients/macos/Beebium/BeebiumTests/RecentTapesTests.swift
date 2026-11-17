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

/// Tests for RecentTapesManager against a private UserDefaults suite, so the
/// host's preferences are left untouched. Tapes keep their own list, apart from
/// recent discs; the policy is the same (section 9.2).
@MainActor
final class RecentTapesTests: XCTestCase {
    private var defaults: UserDefaults!
    private var suiteName: String!

    override func setUp() async throws {
        try await super.setUp()
        suiteName = "recent-tapes-test-\(UUID().uuidString)"
        defaults = UserDefaults(suiteName: suiteName)
        XCTAssertNotNil(defaults)
    }

    override func tearDown() async throws {
        defaults.removePersistentDomain(forName: suiteName)
        defaults = nil
        suiteName = nil
        try await super.tearDown()
    }

    private func paths(_ manager: RecentTapesManager) -> [String] {
        manager.tapes.map(\.filepath)
    }

    func testNewestFirst() {
        let manager = RecentTapesManager(defaults: defaults)
        manager.record(filepath: "/tapes/a.uef")
        manager.record(filepath: "/tapes/b.uef")
        manager.record(filepath: "/tapes/c.uef")
        XCTAssertEqual(paths(manager), ["/tapes/c.uef", "/tapes/b.uef", "/tapes/a.uef"])
    }

    func testDedupeByPathMovesToFront() {
        let manager = RecentTapesManager(defaults: defaults)
        manager.record(filepath: "/tapes/a.uef")
        manager.record(filepath: "/tapes/b.uef")
        manager.record(filepath: "/tapes/a.uef")
        XCTAssertEqual(paths(manager), ["/tapes/a.uef", "/tapes/b.uef"])
    }

    func testCapAtTen() {
        let manager = RecentTapesManager(defaults: defaults)
        for i in 0..<15 {
            manager.record(filepath: "/tapes/tape\(i).uef")
        }
        XCTAssertEqual(manager.tapes.count, 10)
        XCTAssertEqual(paths(manager).first, "/tapes/tape14.uef")
        XCTAssertEqual(paths(manager).last, "/tapes/tape5.uef")
    }

    func testClear() {
        let manager = RecentTapesManager(defaults: defaults)
        manager.record(filepath: "/tapes/a.uef")
        manager.clear()
        XCTAssertTrue(manager.tapes.isEmpty)
        let reloaded = RecentTapesManager(defaults: defaults)
        XCTAssertTrue(reloaded.tapes.isEmpty)
    }

    func testRoundTripThroughPersistence() {
        let manager = RecentTapesManager(defaults: defaults)
        manager.record(filepath: "/tapes/a.uef")
        manager.record(filepath: "/tapes/b.uef")
        let reloaded = RecentTapesManager(defaults: defaults)
        XCTAssertEqual(paths(reloaded), ["/tapes/b.uef", "/tapes/a.uef"])
    }

    // MARK: - Menu labels

    func testMenuLabelsUseFileNameWhenUnique() {
        let tapes = [
            RecentTape(filepath: "/tapes/elite.uef", lastUsed: Date()),
            RecentTape(filepath: "/tapes/chuckie.csw", lastUsed: Date()),
        ]
        XCTAssertEqual(RecentTapesManager.menuLabels(for: tapes), ["elite.uef", "chuckie.csw"])
    }

    func testMenuLabelsDisambiguateDuplicateNamesWithParentFolder() {
        let tapes = [
            RecentTape(filepath: "/games/a/tape.uef", lastUsed: Date()),
            RecentTape(filepath: "/games/b/tape.uef", lastUsed: Date()),
            RecentTape(filepath: "/other/unique.uef", lastUsed: Date()),
        ]
        XCTAssertEqual(
            RecentTapesManager.menuLabels(for: tapes),
            ["tape.uef (a)", "tape.uef (b)", "unique.uef"]
        )
    }
}
