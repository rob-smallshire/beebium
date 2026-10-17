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

    // MARK: - Save / Save As enable rules

    func testSaveEnabledOnlyWithAFocusedMachineWindow() {
        XCTAssertTrue(FileCommands.saveEnabled(hasFocusedMachineWindow: true))
        XCTAssertFalse(FileCommands.saveEnabled(hasFocusedMachineWindow: false))
    }

    func testSaveAsEnabledOnlyForASavedFocusedMachine() {
        XCTAssertTrue(FileCommands.saveAsEnabled(hasFocusedMachineWindow: true, isSaved: true))
        // A transient machine has nothing to duplicate yet.
        XCTAssertFalse(FileCommands.saveAsEnabled(hasFocusedMachineWindow: true, isSaved: false))
        // No machine window focused.
        XCTAssertFalse(FileCommands.saveAsEnabled(hasFocusedMachineWindow: false, isSaved: true))
    }

    // MARK: - Rename enable rules

    func testRenameEnabledOnlyForATransientFocusedMachine() {
        // A transient machine renames in place.
        XCTAssertTrue(FileCommands.renameEnabled(hasFocusedMachineWindow: true, isSaved: false))
        // A saved machine is named by its document; the server refuses a rename.
        XCTAssertFalse(FileCommands.renameEnabled(hasFocusedMachineWindow: true, isSaved: true))
        // No machine window focused.
        XCTAssertFalse(FileCommands.renameEnabled(hasFocusedMachineWindow: false, isSaved: false))
    }

    func testRenameHelpExplainsOnlyWhenUnavailableForASavedMachine() {
        XCTAssertEqual(FileCommands.renameHelp(isSaved: true), FileCommands.renameUnavailableHelp)
        XCTAssertFalse(FileCommands.renameUnavailableHelp.isEmpty)
        // A transient machine renames in place, so there is nothing to explain.
        XCTAssertEqual(FileCommands.renameHelp(isSaved: false), "")
    }
}
