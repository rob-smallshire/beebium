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

/// Tests for RecentDiscsManager against a private UserDefaults suite, so the
/// host's preferences are left untouched.
@MainActor
final class RecentDiscsTests: XCTestCase {
    private var defaults: UserDefaults!
    private var suiteName: String!

    override func setUp() async throws {
        try await super.setUp()
        suiteName = "recent-discs-test-\(UUID().uuidString)"
        defaults = UserDefaults(suiteName: suiteName)
        XCTAssertNotNil(defaults)
    }

    override func tearDown() async throws {
        defaults.removePersistentDomain(forName: suiteName)
        defaults = nil
        suiteName = nil
        try await super.tearDown()
    }

    private func paths(_ manager: RecentDiscsManager) -> [String] {
        manager.discs.map(\.filepath)
    }

    func testNewestFirst() {
        let manager = RecentDiscsManager(defaults: defaults)
        manager.record(filepath: "/discs/a.ssd")
        manager.record(filepath: "/discs/b.ssd")
        manager.record(filepath: "/discs/c.ssd")
        XCTAssertEqual(paths(manager), ["/discs/c.ssd", "/discs/b.ssd", "/discs/a.ssd"])
    }

    func testDedupeByPathMovesToFront() {
        let manager = RecentDiscsManager(defaults: defaults)
        manager.record(filepath: "/discs/a.ssd")
        manager.record(filepath: "/discs/b.ssd")
        manager.record(filepath: "/discs/a.ssd")
        XCTAssertEqual(paths(manager), ["/discs/a.ssd", "/discs/b.ssd"])
    }

    func testCapAtTen() {
        let manager = RecentDiscsManager(defaults: defaults)
        for i in 0..<15 {
            manager.record(filepath: "/discs/disc\(i).ssd")
        }
        XCTAssertEqual(manager.discs.count, 10)
        // The ten most recent survive, newest first; the five oldest are gone.
        XCTAssertEqual(paths(manager).first, "/discs/disc14.ssd")
        XCTAssertEqual(paths(manager).last, "/discs/disc5.ssd")
    }

    func testClear() {
        let manager = RecentDiscsManager(defaults: defaults)
        manager.record(filepath: "/discs/a.ssd")
        manager.record(filepath: "/discs/b.ssd")
        manager.clear()
        XCTAssertTrue(manager.discs.isEmpty)
        // And a fresh manager on the same suite finds nothing persisted.
        let reloaded = RecentDiscsManager(defaults: defaults)
        XCTAssertTrue(reloaded.discs.isEmpty)
    }

    func testRoundTripThroughPersistence() {
        let manager = RecentDiscsManager(defaults: defaults)
        manager.record(filepath: "/discs/a.ssd")
        manager.record(filepath: "/discs/b.ssd")

        // A new manager reading the same suite sees the same order.
        let reloaded = RecentDiscsManager(defaults: defaults)
        XCTAssertEqual(paths(reloaded), ["/discs/b.ssd", "/discs/a.ssd"])
    }

    // MARK: - Menu labels

    func testMenuLabelsUseFileNameWhenUnique() {
        let discs = [
            RecentDisc(filepath: "/discs/elite.ssd", lastUsed: Date()),
            RecentDisc(filepath: "/games/exile.ssd", lastUsed: Date()),
        ]
        XCTAssertEqual(RecentDiscsManager.menuLabels(for: discs), ["elite.ssd", "exile.ssd"])
    }

    func testMenuLabelsDisambiguateDuplicateNamesWithParentFolder() {
        let discs = [
            RecentDisc(filepath: "/games/a/disc.ssd", lastUsed: Date()),
            RecentDisc(filepath: "/games/b/disc.ssd", lastUsed: Date()),
            RecentDisc(filepath: "/other/unique.ssd", lastUsed: Date()),
        ]
        XCTAssertEqual(
            RecentDiscsManager.menuLabels(for: discs),
            ["disc.ssd (a)", "disc.ssd (b)", "unique.ssd"]
        )
    }
}
