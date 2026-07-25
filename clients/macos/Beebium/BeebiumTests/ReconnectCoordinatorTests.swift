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

@MainActor
final class ReconnectCoordinatorTests: XCTestCase {

    /// Poll `condition` on the main actor until true or the deadline passes.
    private func waitUntil(_ condition: @escaping () -> Bool,
                           timeout: TimeInterval = 2.0) async {
        let deadline = Date().addingTimeInterval(timeout)
        while !condition() && Date() < deadline {
            try? await Task.sleep(nanoseconds: 2_000_000)  // 2 ms
        }
    }

    func testBackoffDelayDoublesToAnEightSecondCap() {
        XCTAssertEqual(ReconnectCoordinator.backoffDelay(forAttempt: 1), 0.5)
        XCTAssertEqual(ReconnectCoordinator.backoffDelay(forAttempt: 2), 1.0)
        XCTAssertEqual(ReconnectCoordinator.backoffDelay(forAttempt: 3), 2.0)
        XCTAssertEqual(ReconnectCoordinator.backoffDelay(forAttempt: 4), 4.0)
        XCTAssertEqual(ReconnectCoordinator.backoffDelay(forAttempt: 5), 8.0)
        XCTAssertEqual(ReconnectCoordinator.backoffDelay(forAttempt: 6), 8.0)  // capped
        XCTAssertEqual(ReconnectCoordinator.backoffDelay(forAttempt: 20), 8.0)
    }

    func testUnexpectedDropBeginsReconnecting() {
        let coordinator = ReconnectCoordinator()
        coordinator.configure(reconnect: {}, isIntentionalStop: { false })

        coordinator.handleConnectionState(.error("Connection lost"))

        XCTAssertEqual(coordinator.phase, .reconnecting(attempt: 1))
    }

    func testIntentionalStopIsNotFought() {
        let coordinator = ReconnectCoordinator()
        var reconnects = 0
        coordinator.configure(reconnect: { reconnects += 1 },
                              isIntentionalStop: { true })

        coordinator.handleConnectionState(.error("Server stopped"))

        XCTAssertEqual(coordinator.phase, .idle)
        XCTAssertEqual(reconnects, 0)
    }

    func testConnectedResetsToIdle() {
        let coordinator = ReconnectCoordinator()
        coordinator.configure(reconnect: {}, isIntentionalStop: { false })

        coordinator.handleConnectionState(.error("Connection lost"))
        XCTAssertEqual(coordinator.phase, .reconnecting(attempt: 1))

        coordinator.handleConnectionState(.connected)
        XCTAssertEqual(coordinator.phase, .idle)
    }

    func testCancelStopsAndSuppresses() {
        let coordinator = ReconnectCoordinator()
        var reconnects = 0
        coordinator.configure(reconnect: { reconnects += 1 },
                              isIntentionalStop: { false })

        coordinator.handleConnectionState(.error("Connection lost"))
        coordinator.cancel()
        XCTAssertEqual(coordinator.phase, .idle)

        // A further drop after cancel is ignored (user gave up).
        coordinator.handleConnectionState(.error("Connection lost"))
        XCTAssertEqual(coordinator.phase, .idle)
        XCTAssertEqual(reconnects, 0)
    }

    func testWakeForcesAnImmediateAttempt() async {
        let coordinator = ReconnectCoordinator()
        var reconnects = 0
        coordinator.configure(reconnect: { reconnects += 1 },
                              isIntentionalStop: { false })

        coordinator.handleWake()
        XCTAssertEqual(coordinator.phase, .reconnecting(attempt: 1))

        await waitUntil { reconnects >= 1 }
        XCTAssertEqual(reconnects, 1)
    }

    func testGivesUpAfterMaxAttempts() async {
        let coordinator = ReconnectCoordinator()
        var reconnects = 0
        // Each reconnect "fails": report the drop again to drive the next try.
        // Zero backoff keeps the whole loop within the test's patience.
        coordinator.configure(
            reconnect: {
                reconnects += 1
                coordinator.handleConnectionState(.error("still down"))
            },
            isIntentionalStop: { false },
            backoff: { _ in 0 }
        )

        coordinator.handleConnectionState(.error("down"))

        await waitUntil { coordinator.phase == .givenUp }
        XCTAssertEqual(coordinator.phase, .givenUp)
        XCTAssertEqual(reconnects, ReconnectCoordinator.maxAttempts)
    }

    func testRetryAfterGivingUpResumes() async {
        let coordinator = ReconnectCoordinator()
        var reconnects = 0
        var failing = true
        coordinator.configure(
            reconnect: {
                reconnects += 1
                if failing { coordinator.handleConnectionState(.error("still down")) }
            },
            isIntentionalStop: { false },
            backoff: { _ in 0 }
        )

        coordinator.handleConnectionState(.error("down"))
        await waitUntil { coordinator.phase == .givenUp }

        // The server comes back; a manual retry now succeeds.
        failing = false
        let before = reconnects
        coordinator.retryNow()
        await waitUntil { reconnects > before }
        coordinator.handleConnectionState(.connected)
        XCTAssertEqual(coordinator.phase, .idle)
    }
}
