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

/// The AppKit document behind a machine window. Its window-side behaviour (the
/// popover, move, lock) needs a real window and is checked by hand; the pure
/// seams are the title (the server's plain name) and the file URL, which is the
/// saved directory only -- a transient machine never has one, which is why the
/// document is not attached to a window until the machine is saved.
@MainActor
final class MachineDocumentTests: XCTestCase {

    private func makeDocument(_ client: SystemClient) -> MachineDocument {
        MachineDocument(systemClient: client,
                        recents: MachineRecents(store: InMemoryRecentDocumentsStore()))
    }

    func testDisplayNameUsesTheServerPlainName() {
        let client = SystemClient()
        var identity = Beebium_MachineIdentity()
        identity.name = "Model B (Disc) #2"
        client.updateIdentity(identity)

        XCTAssertEqual(makeDocument(client).displayName, "Model B (Disc) #2")
    }

    func testDisplayNameFallsBackWhenTheServerHasNoNameYet() {
        // No server name: AppKit's own untitled name, which is never empty, so
        // the window is not briefly nameless.
        XCTAssertFalse((makeDocument(SystemClient()).displayName ?? "").isEmpty)
    }

    func testFileURLIsTheDirectoryOnlyWhenSaved() {
        // A transient machine's directory is deleted on shutdown, so it must
        // never become a document file URL (proxy icon / breadcrumb).
        XCTAssertNil(MachineDocument.savedFileURL(
            machineSaved: false, machineDirpath: "/m/x.beebium-machine"))
        XCTAssertNil(MachineDocument.savedFileURL(
            machineSaved: true, machineDirpath: ""))
        XCTAssertEqual(
            MachineDocument.savedFileURL(
                machineSaved: true, machineDirpath: "/m/Elite Server.beebium-machine")?.path,
            "/m/Elite Server.beebium-machine")
    }
}
