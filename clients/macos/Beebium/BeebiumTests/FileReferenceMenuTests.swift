// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
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

/// The FileReference pull-down composition (#144): server actions first in order,
/// then Reveal in Finder only when the server shares this host's filesystem, then
/// Copy Path always.
final class FileReferenceMenuTests: XCTestCase {

    private let reload = FileReferenceMenu.ServerAction(id: "reload", title: "Reload")

    func testLocalServerAddsRevealThenCopyAfterServerActions() {
        XCTAssertEqual(
            FileReferenceMenu.items(serverActions: [reload], isServerLocal: true, hasPath: true),
            [.serverAction(id: "reload", title: "Reload"), .reveal, .copyPath])
    }

    func testRemoteServerOmitsReveal() {
        XCTAssertEqual(
            FileReferenceMenu.items(serverActions: [reload], isServerLocal: false, hasPath: true),
            [.serverAction(id: "reload", title: "Reload"), .copyPath])
    }

    func testNoServerActionsStillOffersClientItems() {
        XCTAssertEqual(
            FileReferenceMenu.items(serverActions: [], isServerLocal: true, hasPath: true),
            [.reveal, .copyPath])
        XCTAssertEqual(
            FileReferenceMenu.items(serverActions: [], isServerLocal: false, hasPath: true),
            [.copyPath])
    }

    func testServerActionsKeepTheirOrder() {
        let actions = [FileReferenceMenu.ServerAction(id: "a", title: "A"),
                       FileReferenceMenu.ServerAction(id: "b", title: "B")]
        XCTAssertEqual(
            FileReferenceMenu.items(serverActions: actions, isServerLocal: false, hasPath: true),
            [.serverAction(id: "a", title: "A"), .serverAction(id: "b", title: "B"), .copyPath])
    }

    // #170 (1): an empty path (e.g. map-file=none) offers neither client-side
    // action, whether or not the server is local; server actions remain.
    func testEmptyPathOffersNoClientActions() {
        XCTAssertEqual(
            FileReferenceMenu.items(serverActions: [reload], isServerLocal: true, hasPath: false),
            [.serverAction(id: "reload", title: "Reload")])
        XCTAssertEqual(
            FileReferenceMenu.items(serverActions: [reload], isServerLocal: false, hasPath: false),
            [.serverAction(id: "reload", title: "Reload")])
    }

    func testEmptyPathAndNoServerActionsIsAnEmptyMenu() {
        XCTAssertEqual(
            FileReferenceMenu.items(serverActions: [], isServerLocal: true, hasPath: false), [])
        XCTAssertEqual(
            FileReferenceMenu.items(serverActions: [], isServerLocal: false, hasPath: false), [])
    }
}
