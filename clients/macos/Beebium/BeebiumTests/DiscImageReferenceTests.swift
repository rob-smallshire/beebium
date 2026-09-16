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

/// URL.resolvingDiscImageReference() must turn a Finder alias into its target,
/// and leave everything else (symlink, plain file, missing path) untouched
/// without throwing.
final class DiscImageReferenceTests: XCTestCase {
    private var dir: URL!

    override func setUpWithError() throws {
        dir = URL(fileURLWithPath: NSTemporaryDirectory())
            .appendingPathComponent("beebium-alias-tests-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
    }

    override func tearDownWithError() throws {
        try? FileManager.default.removeItem(at: dir)
    }

    /// Normalise for comparison: NSTemporaryDirectory() is under a /var -> /private/var
    /// symlink, and alias resolution may return either spelling.
    private func canonical(_ url: URL) -> String {
        url.resolvingSymlinksInPath().path
    }

    func testAliasResolvesToTarget() throws {
        let target = dir.appendingPathComponent("disc.ssd")
        try Data("disc bytes".utf8).write(to: target)

        let aliasURL = dir.appendingPathComponent("disc-alias")
        let bookmark = try target.bookmarkData(options: .suitableForBookmarkFile,
                                               includingResourceValuesForKeys: nil,
                                               relativeTo: nil)
        try URL.writeBookmarkData(bookmark, to: aliasURL)

        // Sanity: the alias file really is an alias, and is not the target.
        XCTAssertNotEqual(canonical(aliasURL), canonical(target))
        let resolved = aliasURL.resolvingDiscImageReference()
        XCTAssertEqual(canonical(resolved), canonical(target),
                       "an alias must resolve to its target file")
    }

    func testSymlinkPassesThroughUnchanged() throws {
        let target = dir.appendingPathComponent("real.ssd")
        try Data("real".utf8).write(to: target)
        let link = dir.appendingPathComponent("link.ssd")
        try FileManager.default.createSymbolicLink(at: link, withDestinationURL: target)

        // A symlink is not a Finder alias, so it is returned unchanged (POSIX
        // open follows it anyway).
        XCTAssertEqual(link.resolvingDiscImageReference().path, link.path)
    }

    func testPlainFilePassesThroughUnchanged() throws {
        let file = dir.appendingPathComponent("plain.ssd")
        try Data("x".utf8).write(to: file)
        XCTAssertEqual(file.resolvingDiscImageReference().path, file.path)
    }

    func testNonexistentPathPassesThroughWithoutThrowing() {
        let missing = dir.appendingPathComponent("gone.ssd")
        XCTAssertEqual(missing.resolvingDiscImageReference().path, missing.path,
                       "a missing path is returned unchanged and must not throw")
    }
}
