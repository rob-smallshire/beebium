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

/// Semantics that only show up in timing, which the interactive UI can't
/// exercise cheaply: pausing must genuinely hold the message (not clear it),
/// resuming must give it a fresh lifetime, and a superseded message's cancelled
/// timer must not wipe the message that replaced it.
@MainActor
final class TransientMessageTests: XCTestCase {
    // A short lifetime keeps the timing tests fast; the waits below are
    // generously past it to stay robust on a loaded CI machine.
    private let lifetime = 0.2
    private var pastLifetimeNanos: UInt64 { UInt64(lifetime * 3 * 1_000_000_000) }
    private var withinLifetimeNanos: UInt64 { UInt64(lifetime * 0.3 * 1_000_000_000) }

    func testShowClearsItselfAfterLifetime() async throws {
        let message = TransientMessage(lifetime: lifetime)
        message.show("A")
        XCTAssertEqual(message.brief, "A")
        try await Task.sleep(nanoseconds: pastLifetimeNanos)
        XCTAssertNil(message.content, "a shown message should clear itself after its lifetime")
    }

    func testPauseHoldsMessagePastLifetime() async throws {
        let message = TransientMessage(lifetime: lifetime)
        message.show("A")
        message.pauseAutoClear()
        try await Task.sleep(nanoseconds: pastLifetimeNanos)
        // If cancelling the timer fell through to the clear (the classic
        // try?-swallows-CancellationError bug), pausing would have cleared the
        // message immediately and this would be nil.
        XCTAssertEqual(message.brief, "A", "pause must hold the message, not clear it")
    }

    func testResumeClearsAfterFreshLifetime() async throws {
        let message = TransientMessage(lifetime: lifetime)
        message.show("A")
        message.pauseAutoClear()
        message.resumeAutoClear()
        try await Task.sleep(nanoseconds: pastLifetimeNanos)
        XCTAssertNil(message.content, "resume must let the message clear after a fresh lifetime")
    }

    func testRapidShowKeepsLatest() async throws {
        let message = TransientMessage(lifetime: lifetime)
        message.show("A")
        message.show("B")
        // Long enough that a wrongly-immediate wake of A's cancelled timer would
        // have wiped B, but well within the lifetime so B's own timer hasn't
        // fired yet.
        try await Task.sleep(nanoseconds: withinLifetimeNanos)
        XCTAssertEqual(message.brief, "B",
                       "a superseded message's cancelled timer must not wipe the new one")
    }
}
