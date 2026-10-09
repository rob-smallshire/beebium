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

/// Tests the mapping from a resolve-preset diagnostic's JSON-pointer path to a
/// human location and to the configuration section it belongs to.
final class PresetDiagnosticLocationTests: XCTestCase {

    func testDescribesSectionsAndRows() {
        XCTAssertEqual(PresetDiagnosticLocation.describe(path: "/storage/floppy_drives/0/image_uri"), "Floppy 0")
        XCTAssertEqual(PresetDiagnosticLocation.describe(path: "/storage/fdc_socket/id"), "Storage")
        XCTAssertEqual(PresetDiagnosticLocation.describe(path: "/sideways_bank/slots/2/image_uri"), "Sideways slot 2")
        XCTAssertEqual(PresetDiagnosticLocation.describe(path: "/extensions/1/config/scsi-id"), "Extension 1")
        XCTAssertEqual(PresetDiagnosticLocation.describe(path: "/econet/transport"), "Econet")
    }

    func testWholeDocumentPathHasNoLocation() {
        XCTAssertNil(PresetDiagnosticLocation.describe(path: ""))
    }

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
