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

/// When closing a window should first offer to save the machine.
final class MachineSavingCloseTests: XCTestCase {

    func testPromptsWhenWrittenAndTheCloseShutsTheMachineDown() {
        XCTAssertTrue(MachineSaving.shouldPromptToSaveOnClose(
            closeAction: .shutdownServer, templateMediaWritten: true))
    }

    func testNoPromptWhenNothingHasBeenWritten() {
        XCTAssertFalse(MachineSaving.shouldPromptToSaveOnClose(
            closeAction: .shutdownServer, templateMediaWritten: false))
    }

    func testNoPromptWhenTheCloseOnlyDisconnects() {
        // A disconnect leaves the machine running (external, or unlinked), so
        // nothing is lost and there is nothing to ask.
        XCTAssertFalse(MachineSaving.shouldPromptToSaveOnClose(
            closeAction: .disconnect, templateMediaWritten: true))
    }

    func testNoPromptWhenOtherClientsKeepTheMachineRunning() {
        XCTAssertFalse(MachineSaving.shouldPromptToSaveOnClose(
            closeAction: .promptUser(clientCount: 2), templateMediaWritten: true))
    }
}
