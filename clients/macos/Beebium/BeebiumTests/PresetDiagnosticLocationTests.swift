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

/// Tests the structural routing of a resolve-preset diagnostic's JSON-pointer
/// path to the configuration section it belongs to. The human location text is
/// now produced by resolve-preset (PresetDiagnostic.location), not the client.
final class PresetDiagnosticLocationTests: XCTestCase {

    func testExtensionIndex() {
        XCTAssertEqual(PresetDiagnosticLocation.extensionIndex(path: "/extensions/2"), 2)
        XCTAssertEqual(PresetDiagnosticLocation.extensionIndex(path: "/extensions/0/config/scsi-id"), 0)
        XCTAssertNil(PresetDiagnosticLocation.extensionIndex(path: "/storage/floppy_drives/0"))
        XCTAssertNil(PresetDiagnosticLocation.extensionIndex(path: ""))
    }

    func testTopLevelKey() {
        XCTAssertEqual(PresetDiagnosticLocation.topLevelKey(path: "/extensions/2/config/image"), "extensions")
        XCTAssertEqual(PresetDiagnosticLocation.topLevelKey(path: "/storage/floppy_drives/0"), "storage")
        XCTAssertNil(PresetDiagnosticLocation.topLevelKey(path: ""))
    }
}
