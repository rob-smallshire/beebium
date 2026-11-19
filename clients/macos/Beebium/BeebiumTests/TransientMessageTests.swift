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

    func testTapeChangeClearsAnOrdinaryFailure() {
        // An ordinary failure is about the tape that was in; a tape change clears
        // it, as the deck has always done.
        let message = TransientMessage(lifetime: lifetime)
        message.showFailure("Live input ended", detail: "The server went away.")
        message.clearForTapeChange()
        XCTAssertNil(message.kind, "a plain failure should clear when the tape changes")
    }

    func testTapeChangeClearsANotice() {
        let message = TransientMessage(lifetime: lifetime)
        message.show("Eject disc first")
        message.clearForTapeChange()
        XCTAssertNil(message.kind, "a notice should clear when the tape changes")
    }

    func testADeviceChangeFailureOutlivesTheEject() {
        // Round 12: the input device changing ejects the live tape a moment after
        // the failure is shown. The reason to choose the device again must survive
        // that eject, or it is wiped before the user can read it -- the bug.
        let message = TransientMessage(lifetime: lifetime)
        message.showFailure(
            "The input device changed while the live tape was playing; choose it again from Input Device.",
            detail: "Built-in Microphone: 48000 Hz, 2 channels, now 44100 Hz",
            outlivesTape: true)
        message.clearForTapeChange()
        let failure = try? XCTUnwrap(message.failure,
                                     "a device-change failure must survive the eject it triggers")
        XCTAssertEqual(failure?.brief,
                       "The input device changed while the live tape was playing; "
                       + "choose it again from Input Device.")
    }

    func testTheUserCanStillDismissAnOutlivingFailure() {
        // Outliving the tape change does not make it permanent: the user's own
        // dismiss (clear) still removes it.
        let message = TransientMessage(lifetime: lifetime)
        message.showFailure("The input device was disconnected while the live tape was playing.",
                            detail: "Built-in Microphone is no longer available.",
                            outlivesTape: true)
        message.clear()
        XCTAssertNil(message.kind, "the user's dismiss clears even a tape-outliving failure")
    }

    func testANewOrdinaryOutcomeStopsOutlivingTheTape() {
        // Once a device-change failure is replaced by an ordinary outcome, that
        // new message is ordinary again and a tape change clears it.
        let message = TransientMessage(lifetime: lifetime)
        message.showFailure("The input device was disconnected while the live tape was playing.",
                            detail: "Built-in Microphone is no longer available.",
                            outlivesTape: true)
        XCTAssertTrue(message.outlivesTape)
        message.showFailure("Insert failed", detail: "Cannot open tape image: /x")
        XCTAssertFalse(message.outlivesTape, "a plain failure does not outlive the tape")
        message.clearForTapeChange()
        XCTAssertNil(message.kind)
    }
}
