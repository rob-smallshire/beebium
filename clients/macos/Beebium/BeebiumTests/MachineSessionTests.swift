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

/// The transient-to-saved document state and the session that drives it.
@MainActor
final class MachineSessionTests: XCTestCase {

    func testTransientMachineHasNoDocumentFile() {
        let state = MachineDocumentState()
        XCTAssertFalse(state.isSaved)
        XCTAssertNil(state.representedURL)
        XCTAssertFalse(state.hasUnsavedChanges)
    }

    func testMarkSavedAdoptsDirectoryAndClearsUnsavedChanges() {
        var state = MachineDocumentState()
        state.hasUnsavedChanges = true
        let dirpath = URL(fileURLWithPath: "/m/Elite.beebium-machine")
        state.markSaved(to: dirpath)
        XCTAssertTrue(state.isSaved)
        XCTAssertEqual(state.representedURL, dirpath)
        XCTAssertFalse(state.hasUnsavedChanges)
    }

    func testSessionAdoptingASavedDirectoryRemembersItInRecents() {
        let store = InMemoryRecentDocumentsStore()
        let session = MachineSession(recents: MachineRecents(store: store))
        let dirpath = URL(fileURLWithPath: "/m/Elite.beebium-machine")
        session.adoptSavedDirectory(dirpath)
        XCTAssertEqual(session.documentState.representedURL, dirpath)
        XCTAssertEqual(store.recentURLs.map(\.path), ["/m/Elite.beebium-machine"])
    }

    func testSettingUnsavedChangesShowsThem() {
        let session = MachineSession()
        session.setHasUnsavedChanges(true)
        XCTAssertTrue(session.documentState.hasUnsavedChanges)
    }
}
