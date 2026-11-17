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

/// Tests for DialogLocations (client specification 3.6): a file dialog remembers
/// its last directory per function and direction, falls back to Documents when a
/// remembered directory is gone or (for a save) unwritable, and never remembers a
/// template or state directory. Runs against a private UserDefaults suite and
/// temporary directories, so the host is left untouched.
@MainActor
final class DialogLocationsTests: XCTestCase {
    private var defaults: UserDefaults!
    private var suiteName: String!
    private var tempRoot: URL!

    override func setUp() async throws {
        try await super.setUp()
        suiteName = "dialog-locations-test-\(UUID().uuidString)"
        defaults = UserDefaults(suiteName: suiteName)
        tempRoot = URL(fileURLWithPath: NSTemporaryDirectory())
            .appendingPathComponent("dialog-locations-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: tempRoot, withIntermediateDirectories: true)
    }

    override func tearDown() async throws {
        defaults.removePersistentDomain(forName: suiteName)
        try? FileManager.default.removeItem(at: tempRoot)
        defaults = nil
        suiteName = nil
        tempRoot = nil
        try await super.tearDown()
    }

    private func store(excluding excludedRoots: [URL] = []) -> DialogLocations {
        DialogLocations(defaults: defaults, fileManager: .default, excludedRoots: excludedRoots)
    }

    private func makeDirectory(_ name: String) throws -> URL {
        let url = tempRoot.appendingPathComponent(name)
        try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }

    private var documents: URL {
        (try? FileManager.default.url(for: .documentDirectory, in: .userDomainMask,
                                      appropriateFor: nil, create: false))
            ?? FileManager.default.homeDirectoryForCurrentUser
    }

    private func samePath(_ a: URL, _ b: URL) -> Bool {
        a.resolvingSymlinksInPath().standardizedFileURL.path
            == b.resolvingSymlinksInPath().standardizedFileURL.path
    }

    func testFirstUseStartsInDocuments() {
        let locations = store()
        XCTAssertTrue(samePath(locations.startDirectory(for: .insertTape), documents),
                      "a function with nothing remembered starts in Documents")
    }

    func testRemembersAndRecallsPerFunction() throws {
        let locations = store()
        let folder = try makeDirectory("tapes")
        locations.remember(folder.appendingPathComponent("elite.uef"), for: .insertTape)
        XCTAssertTrue(samePath(locations.startDirectory(for: .insertTape), folder))
        // A different function is unaffected and still starts in Documents.
        XCTAssertTrue(samePath(locations.startDirectory(for: .newBlankTape), documents),
                      "each function remembers separately")
    }

    func testOpenAndSaveKeysAreDistinct() throws {
        let locations = store()
        let opened = try makeDirectory("collection")
        let saved = try makeDirectory("my-work")
        locations.remember(opened.appendingPathComponent("a.ssd"), for: .insertDisc)
        locations.remember(saved.appendingPathComponent("b.ssd"), for: .newBlankDisc)
        XCTAssertTrue(samePath(locations.startDirectory(for: .insertDisc), opened))
        XCTAssertTrue(samePath(locations.startDirectory(for: .newBlankDisc), saved),
                      "open and save never share a remembered location")
    }

    func testFallsBackToDocumentsWhenRememberedIsGone() throws {
        let locations = store()
        let folder = try makeDirectory("gone")
        locations.remember(folder.appendingPathComponent("x.uef"), for: .insertTape)
        try FileManager.default.removeItem(at: folder)
        XCTAssertTrue(samePath(locations.startDirectory(for: .insertTape), documents),
                      "a vanished directory falls back to Documents")
    }

    func testSaveFallsBackWhenUnwritableButOpenDoesNot() throws {
        let locations = store()
        let folder = try makeDirectory("read-only")
        locations.remember(folder.appendingPathComponent("x.uef"), for: .newBlankTape)   // save
        locations.remember(folder.appendingPathComponent("x.uef"), for: .insertTape)     // open
        try FileManager.default.setAttributes([.posixPermissions: 0o500], ofItemAtPath: folder.path)
        defer { try? FileManager.default.setAttributes([.posixPermissions: 0o700],
                                                        ofItemAtPath: folder.path) }
        XCTAssertTrue(samePath(locations.startDirectory(for: .newBlankTape), documents),
                      "a save will not reuse an unwritable directory")
        XCTAssertTrue(samePath(locations.startDirectory(for: .insertTape), folder),
                      "an open may still start in a read-only directory")
    }

    func testNeverRemembersAnExcludedDirectory() throws {
        let templates = try makeDirectory("blank-tapes")
        let locations = store(excluding: [templates])
        let inside = templates.appendingPathComponent("Blank C15 (15 minutes).uef")
        locations.remember(inside, for: .insertTape)
        XCTAssertTrue(samePath(locations.startDirectory(for: .insertTape), documents),
                      "choosing a file in a template directory must not change the remembered location")

        // A directory nested below an excluded root is excluded too.
        let nested = try makeDirectory("blank-tapes/sub")
        locations.remember(nested.appendingPathComponent("y.uef"), for: .insertTape)
        XCTAssertTrue(samePath(locations.startDirectory(for: .insertTape), documents))

        // A directory outside every excluded root is remembered normally.
        let ordinary = try makeDirectory("ordinary")
        locations.remember(ordinary.appendingPathComponent("z.uef"), for: .insertTape)
        XCTAssertTrue(samePath(locations.startDirectory(for: .insertTape), ordinary))
    }

    func testNeverStartsInAnExcludedDirectoryEvenIfSomehowStored() throws {
        // Store the excluded path directly (bypassing remember), then confirm a
        // start still refuses it and falls back to Documents.
        let templates = try makeDirectory("blank-discs")
        let locations = store(excluding: [templates])
        defaults.set(templates.path, forKey: "fileDialogDirectory.insertDisc")
        XCTAssertTrue(samePath(locations.startDirectory(for: .insertDisc), documents))
    }
}
