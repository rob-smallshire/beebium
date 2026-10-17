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

/// An in-memory stand-in for the system recent-documents list that models
/// NSDocumentController's documented behaviour: most recent first, and noting an
/// existing URL moves it to the front rather than duplicating it. A seed can be
/// set directly to simulate residual duplicates a move leaves in the real list.
@MainActor
final class InMemoryRecentDocumentsStore: RecentDocumentsStore {
    private(set) var urls: [URL]

    init(seed: [URL] = []) { urls = seed }

    func noteRecent(_ url: URL) {
        urls.removeAll { $0.standardizedFileURL.path == url.standardizedFileURL.path }
        urls.insert(url, at: 0)
    }
    func removeRecent(_ url: URL) {
        urls.removeAll { $0.standardizedFileURL.path == url.standardizedFileURL.path }
    }
    var recentURLs: [URL] { urls }
    func clearRecents() { urls.removeAll() }
}

@MainActor
final class MachineRecentsTests: XCTestCase {

    func testRememberAddsMostRecentFirst() {
        let store = InMemoryRecentDocumentsStore()
        let recents = MachineRecents(store: store)
        recents.remember(machineDirpath: URL(fileURLWithPath: "/m/a.beebium-machine"))
        recents.remember(machineDirpath: URL(fileURLWithPath: "/m/b.beebium-machine"))
        XCTAssertEqual(recents.machineDirpaths.map(\.path),
                       ["/m/b.beebium-machine", "/m/a.beebium-machine"])
    }

    func testRememberingAnExistingMachineMovesItToFrontWithoutDuplicating() {
        let store = InMemoryRecentDocumentsStore()
        let recents = MachineRecents(store: store)
        recents.remember(machineDirpath: URL(fileURLWithPath: "/m/a.beebium-machine"))
        recents.remember(machineDirpath: URL(fileURLWithPath: "/m/b.beebium-machine"))
        recents.remember(machineDirpath: URL(fileURLWithPath: "/m/a.beebium-machine"))
        XCTAssertEqual(recents.machineDirpaths.map(\.path),
                       ["/m/a.beebium-machine", "/m/b.beebium-machine"])
    }

    func testRenameMovesTheEntryFromOldPathToNewAtFront() {
        let store = InMemoryRecentDocumentsStore()
        let recents = MachineRecents(store: store)
        recents.remember(machineDirpath: URL(fileURLWithPath: "/m/a.beebium-machine"))
        recents.remember(machineDirpath: URL(fileURLWithPath: "/m/old.beebium-machine"))
        recents.remember(machineDirpath: URL(fileURLWithPath: "/m/c.beebium-machine"))

        recents.rename(from: URL(fileURLWithPath: "/m/old.beebium-machine"),
                       to: URL(fileURLWithPath: "/m/new.beebium-machine"))

        // The old path is gone, the new path is most recent, and the others keep
        // their order.
        XCTAssertEqual(recents.machineDirpaths.map(\.path),
                       ["/m/new.beebium-machine", "/m/c.beebium-machine", "/m/a.beebium-machine"])
    }

    func testClearEmptiesTheList() {
        let store = InMemoryRecentDocumentsStore(seed: [URL(fileURLWithPath: "/m/a.beebium-machine")])
        let recents = MachineRecents(store: store)
        recents.clear()
        XCTAssertTrue(recents.machineDirpaths.isEmpty)
    }

    func testDuplicatesDifferingOnlyByTrailingSlashCollapse() {
        // A move can leave both forms in the system list; the page shows one.
        let store = InMemoryRecentDocumentsStore(seed: [
            URL(fileURLWithPath: "/m/a.beebium-machine", isDirectory: true),
            URL(fileURLWithPath: "/m/a.beebium-machine", isDirectory: false),
            URL(fileURLWithPath: "/m/b.beebium-machine"),
        ])
        let recents = MachineRecents(store: store)
        XCTAssertEqual(recents.machineDirpaths.map(\.path),
                       ["/m/a.beebium-machine", "/m/b.beebium-machine"])
    }
}
