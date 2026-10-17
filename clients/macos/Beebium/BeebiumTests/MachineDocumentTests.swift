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
import UniformTypeIdentifiers
@testable import Beebium

/// Resolving an opened path down to the machine directory to launch, and the
/// saved-machine document type. The resolver is a pure seam, pinned here
/// without touching the filesystem.
final class MachineDocumentTests: XCTestCase {

    // The resolved directory is compared by `.path`: it is handed to
    // `start --machine <dir>` as a path, and deletingLastPathComponent() yields
    // a trailing slash that `.path` normalises away.

    func testDirectoryPathResolvesToItself() {
        let dir = URL(fileURLWithPath: "/Users/me/Documents/Elite Server.beebium-machine")
        XCTAssertEqual(
            MachineDocument.machineDirectory(forOpenedPath: dir, isDirectory: true).path,
            "/Users/me/Documents/Elite Server.beebium-machine")
    }

    func testInnerDocumentFileResolvesToItsDirectory() {
        // Windows/Linux open the inner file; its parent is the machine.
        let file = URL(fileURLWithPath:
            "/Users/me/Documents/Elite Server.beebium-machine/Elite Server.beebium-machine")
        XCTAssertEqual(
            MachineDocument.machineDirectory(forOpenedPath: file, isDirectory: false).path,
            "/Users/me/Documents/Elite Server.beebium-machine")
    }

    func testPlainFolderWithoutSuffixResolvesToItself() {
        // The server identifies a machine by its machine.json, not the suffix,
        // so a directory renamed in Finder still opens.
        let dir = URL(fileURLWithPath: "/tmp/my machine")
        XCTAssertEqual(
            MachineDocument.machineDirectory(forOpenedPath: dir, isDirectory: true).path,
            "/tmp/my machine")
    }

    func testFileExtensionConstant() {
        XCTAssertEqual(MachineDocument.fileExtension, "beebium-machine")
    }

    func testExportedUTTypeIdentifier() {
        // The identifier is fixed by the exported-type string regardless of
        // Launch Services registration; the package conformance is asserted by
        // the Info.plist declaration, not here.
        XCTAssertEqual(UTType.beebiumMachine.identifier, "com.beebium.machine")
    }
}
