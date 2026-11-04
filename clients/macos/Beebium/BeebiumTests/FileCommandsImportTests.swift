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

    func testSaveEnabledForASavedMachineRegardlessOfServerLocality() {
        // A saved machine saves in place -- no host path is handed over -- so it
        // is available whether the server is local or remote.
        XCTAssertTrue(FileCommands.saveEnabled(
            hasFocusedMachineWindow: true, isSaved: true, isServerLocal: true))
        XCTAssertTrue(FileCommands.saveEnabled(
            hasFocusedMachineWindow: true, isSaved: true, isServerLocal: false))
    }

    func testFirstSaveOfATransientMachineNeedsALocalServer() {
        // A transient machine's first save hands the server a directory chosen on
        // this host, so it is offered only when the server shares this filesystem.
        XCTAssertTrue(FileCommands.saveEnabled(
            hasFocusedMachineWindow: true, isSaved: false, isServerLocal: true))
        XCTAssertFalse(FileCommands.saveEnabled(
            hasFocusedMachineWindow: true, isSaved: false, isServerLocal: false))
    }

    func testSaveDisabledWithNoFocusedMachineWindow() {
        XCTAssertFalse(FileCommands.saveEnabled(
            hasFocusedMachineWindow: false, isSaved: true, isServerLocal: true))
    }

    func testSaveAsEnabledOnlyForASavedFocusedMachineOnALocalServer() {
        XCTAssertTrue(FileCommands.saveAsEnabled(
            hasFocusedMachineWindow: true, isSaved: true, isServerLocal: true))
        // A transient machine has nothing to duplicate yet.
        XCTAssertFalse(FileCommands.saveAsEnabled(
            hasFocusedMachineWindow: true, isSaved: false, isServerLocal: true))
        // No machine window focused.
        XCTAssertFalse(FileCommands.saveAsEnabled(
            hasFocusedMachineWindow: false, isSaved: true, isServerLocal: true))
        // Save As writes to a host path chosen here, so a remote server withholds it.
        XCTAssertFalse(FileCommands.saveAsEnabled(
            hasFocusedMachineWindow: true, isSaved: true, isServerLocal: false))
    }

    func testSaveAndSaveAsHelpExplainARemoteServer() {
        // The tooltip explains why the item is withheld, only in the remote case.
        XCTAssertTrue(FileCommands.saveHelp(isSaved: false, isServerLocal: false)
            .contains("another host"))
        XCTAssertEqual(FileCommands.saveHelp(isSaved: false, isServerLocal: true), "")
        XCTAssertEqual(FileCommands.saveHelp(isSaved: true, isServerLocal: false), "",
                       "a saved machine saves in place, so Save is not withheld")
        XCTAssertTrue(FileCommands.saveAsHelp(isServerLocal: false).contains("another host"))
        XCTAssertEqual(FileCommands.saveAsHelp(isServerLocal: true), "")
    }

    // MARK: - Rename enable rule

    func testRenameEnabledOnlyForATransientFocusedMachine() {
        // A saved machine renames through the title bar's document popover, so
        // File > Rename Machine... is only for a focused, not-yet-saved machine.
        XCTAssertTrue(FileCommands.renameEnabled(hasFocusedMachineWindow: true, isSaved: false))
        XCTAssertFalse(FileCommands.renameEnabled(hasFocusedMachineWindow: true, isSaved: true))
        XCTAssertFalse(FileCommands.renameEnabled(hasFocusedMachineWindow: false, isSaved: false))
    }
}
