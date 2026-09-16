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

/// The two message kinds behave oppositely in time, which is the whole point:
/// a self-explanatory notice must clear itself, while a failure carrying the
/// server's words must persist until the user (or a new outcome) removes it.
@MainActor
final class TransientMessageTests: XCTestCase {
    // A short lifetime keeps the timing tests fast; waits are generously past it
    // to stay robust on a loaded CI machine.
    private let lifetime = 0.2
    private var pastLifetimeNanos: UInt64 { UInt64(lifetime * 3 * 1_000_000_000) }

    func testNoticeClearsItselfAfterLifetime() async throws {
        let message = TransientMessage(lifetime: lifetime)
        message.show("Eject disc first")
        XCTAssertEqual(message.noticeText, "Eject disc first")
        try await Task.sleep(nanoseconds: pastLifetimeNanos)
        XCTAssertNil(message.kind, "a self-explanatory notice should clear itself")
    }

    func testFailureDoesNotTimeOut() async throws {
        let message = TransientMessage(lifetime: lifetime)
        message.showFailure("Insert failed", detail: "Cannot open disc image: /x")
        try await Task.sleep(nanoseconds: pastLifetimeNanos)
        // A failure carries the server's words; losing it on a timer is exactly
        // the bug this design removes.
        let failure = try XCTUnwrap(message.failure, "a failure must persist past the notice lifetime")
        XCTAssertEqual(failure.brief, "Insert failed")
        XCTAssertEqual(failure.detail, "Cannot open disc image: /x")
    }

    func testClearRemovesFailure() {
        let message = TransientMessage(lifetime: lifetime)
        message.showFailure("Insert failed", detail: "Empty disc image: /x")
        XCTAssertNotNil(message.failure)
        message.clear()
        XCTAssertNil(message.kind, "Clear must remove the failure marker")
    }

    func testNewOutcomeReplacesPreviousFailure() {
        let message = TransientMessage(lifetime: lifetime)
        message.showFailure("Insert failed", detail: "Empty disc image: /a")
        let firstToken = message.failureToken
        message.showFailure("Insert failed", detail: "Unrecognised disc image format: /b")
        let failure = message.failure
        XCTAssertEqual(failure?.detail, "Unrecognised disc image format: /b",
                       "a fresh failure on the same drive must replace the old one")
        XCTAssertGreaterThan(message.failureToken, firstToken,
                             "each failure bumps the token so the UI can open its popover once")
    }

    func testNoticeReplacesFailure() {
        // A subsequent short notice (e.g. a precondition) supersedes a failure.
        let message = TransientMessage(lifetime: lifetime)
        message.showFailure("Insert failed", detail: "Cannot open disc image: /x")
        message.show("Eject disc first")
        XCTAssertNil(message.failure)
        XCTAssertEqual(message.noticeText, "Eject disc first")
    }
}
