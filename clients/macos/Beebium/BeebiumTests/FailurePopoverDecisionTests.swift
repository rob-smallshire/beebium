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

/// The popover-first behaviour, tested as the pure decision it is (the SwiftUI
/// view calls straight into this). "Open once per failure": the first failure,
/// a replacement failure, and a re-created view with an existing failure each
/// open; a re-layout after close-but-keep does not; a notice/refusal/nothing
/// never opens.
final class FailurePopoverDecisionTests: XCTestCase {
    func testFirstFailurePresentsAndRecordsToken() {
        let d = FailurePopoverDecision.decide(hasFailure: true, failureToken: 1, presentedToken: 0)
        XCTAssertTrue(d.present)
        XCTAssertEqual(d.presentedToken, 1)
    }

    func testSameTokenAfterCloseKeepDoesNotRepresent() {
        // Marker kept, popover closed; a re-layout re-runs the check with the
        // same token already presented -- must not reopen.
        let d = FailurePopoverDecision.decide(hasFailure: true, failureToken: 1, presentedToken: 1)
        XCTAssertFalse(d.present)
        XCTAssertEqual(d.presentedToken, 1)
    }

    func testReplacementFailurePresents() {
        let d = FailurePopoverDecision.decide(hasFailure: true, failureToken: 2, presentedToken: 1)
        XCTAssertTrue(d.present)
        XCTAssertEqual(d.presentedToken, 2)
    }

    func testFailureAfterClearPresents() {
        // clear() leaves failureToken untouched; a new failure bumps it, so the
        // guard (token != presented) still opens it.
        let d = FailurePopoverDecision.decide(hasFailure: true, failureToken: 2, presentedToken: 1)
        XCTAssertTrue(d.present)
    }

    func testNoFailureNeverPresents() {
        // A notice, a refusal, or nothing: no failure, so never a popover --
        // whatever the tokens happen to be.
        let notice = FailurePopoverDecision.decide(hasFailure: false, failureToken: 3, presentedToken: 0)
        XCTAssertFalse(notice.present)
        XCTAssertEqual(notice.presentedToken, 0, "presentedToken is unchanged when there is no failure")
        let steady = FailurePopoverDecision.decide(hasFailure: false, failureToken: 3, presentedToken: 3)
        XCTAssertFalse(steady.present)
    }

    func testRecreatedViewWithExistingFailurePresentsOnce() {
        // View recreated: presentedToken resets to 0, but the model still holds
        // a failure at token N > 0 (the onAppear path). Opens once...
        let first = FailurePopoverDecision.decide(hasFailure: true, failureToken: 4, presentedToken: 0)
        XCTAssertTrue(first.present)
        XCTAssertEqual(first.presentedToken, 4)
        // ...and not again for the same token.
        let again = FailurePopoverDecision.decide(hasFailure: true, failureToken: 4,
                                                  presentedToken: first.presentedToken)
        XCTAssertFalse(again.present)
    }

    // MARK: - Call-site mapping through the model

    @MainActor
    func testInsertFailureIsAPresentableFailure() {
        // An insert failure -> a persistent failure marked "Insert failed" with
        // the server's message as detail, which the decision then opens.
        let message = TransientMessage(lifetime: 0.2)
        message.showFailure("Insert failed", detail: "Cannot open disc image: /x")
        XCTAssertEqual(message.failure?.brief, "Insert failed")
        XCTAssertEqual(message.failure?.detail, "Cannot open disc image: /x")
        let d = FailurePopoverDecision.decide(hasFailure: message.failure != nil,
                                              failureToken: message.failureToken,
                                              presentedToken: 0)
        XCTAssertTrue(d.present, "an insert failure should open the popover")
    }

    @MainActor
    func testPreconditionIsANoticeWithNoPopover() {
        // "Eject disc first" is a notice: no failure, so the decision never
        // opens a popover for it.
        let message = TransientMessage(lifetime: 0.2)
        message.show("Eject disc first")
        XCTAssertEqual(message.noticeText, "Eject disc first")
        XCTAssertNil(message.failure)
        let d = FailurePopoverDecision.decide(hasFailure: message.failure != nil,
                                              failureToken: message.failureToken,
                                              presentedToken: 0)
        XCTAssertFalse(d.present, "a precondition notice must not open a popover")
    }
}
