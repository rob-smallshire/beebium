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

final class WelcomePageTests: XCTestCase {
    func testInitialPageIsMachinesWhenThereAreRecents() {
        XCTAssertEqual(WelcomePage.initial(hasRecentMachines: true), .machines)
    }
    func testInitialPageIsPresetsForANewUser() {
        XCTAssertEqual(WelcomePage.initial(hasRecentMachines: false), .presets)
    }
}

@MainActor
final class SavedMachinesModelTests: XCTestCase {

    private func description(_ name: String, dirpath: String) -> MachineDescription {
        MachineDescription(
            machineDirpath: dirpath, name: name, uuid: name, model: "model-b",
            preset: nil, created: "2026-01-01T00:00:00Z", lastUsed: nil,
            thumbnailFilepath: nil, lockedByPid: nil)
    }

    func testReloadDescribesRecentsInOrderAndDropsUnreadable() async {
        let a = URL(fileURLWithPath: "/m/a.beebium-machine")
        let b = URL(fileURLWithPath: "/m/b.beebium-machine")   // deleted: describes to nil
        let c = URL(fileURLWithPath: "/m/c.beebium-machine")
        let store = InMemoryRecentDocumentsStore(seed: [a, b, c])
        let model = SavedMachinesModel(recents: MachineRecents(store: store)) { url in
            url.path == b.path ? nil
                : self.description(url.deletingPathExtension().lastPathComponent, dirpath: url.path)
        }

        await model.reload()

        XCTAssertEqual(model.machines.map(\.name), ["a", "c"])
    }

    func testClearEmptiesBothTheListAndRecents() async {
        let store = InMemoryRecentDocumentsStore(seed: [URL(fileURLWithPath: "/m/a.beebium-machine")])
        let model = SavedMachinesModel(recents: MachineRecents(store: store)) { _ in nil }
        XCTAssertTrue(model.hasRecentMachines)

        model.clear()

        XCTAssertTrue(model.machines.isEmpty)
        XCTAssertFalse(model.hasRecentMachines)
        XCTAssertTrue(store.recentURLs.isEmpty)
    }
}
