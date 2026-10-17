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

/// The wording of the multi-select import's failure summary: nothing when all
/// imports succeed, the one name when a single file fails, and the count plus
/// the list when several do.
final class FileCommandsImportTests: XCTestCase {

    func testNoFailuresGivesNoMessage() {
        XCTAssertNil(FileCommands.importFailureMessage(failures: [], total: 3))
    }

    func testSingleFailureNamesTheFile() {
        let message = FileCommands.importFailureMessage(
            failures: ["elite.beebium-preset"], total: 1)
        XCTAssertEqual(message, "Could not import elite.beebium-preset. Check the console for details.")
    }

    func testSeveralFailuresGiveCountAndList() {
        let message = FileCommands.importFailureMessage(
            failures: ["a.beebium-preset", "b.beebium-preset"], total: 3)
        XCTAssertEqual(
            message,
            "Could not import 2 of 3 presets: a.beebium-preset, b.beebium-preset. Check the console for details.")
    }
}
