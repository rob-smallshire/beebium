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

import Foundation

/// Drives active re-establishment of a dropped connection.
///
/// Re-establishing every stream is expressed as a single reconnect of the
/// VideoClient (it owns the shared channel; the other clients reconnect off the
/// `.connected` cascade), so this coordinator's whole job is *when* to fire that
/// reconnect and how long to keep trying. It reacts to connection-state changes
/// and to host power events:
///
/// - An unexpected drop (`connectionState == .error`, not an intentional stop)
///   starts a backoff loop: reconnect, and on each renewed failure wait a
///   little longer before the next try, up to `maxAttempts`, then give up to a
///   terminal state the user can retry.
/// - A wake from full system sleep almost certainly killed the connection, so
///   it forces an immediate attempt and resets the backoff -- the accelerant
///   that makes recovery prompt instead of waiting out a stale delay.
/// - A success (`.connected`) or an intentional stop resets the loop.
///
/// The reconnect action and the "is this stop intentional?" query are injected,
/// so the decision logic is unit-testable without gRPC or AppKit.
@MainActor
final class ReconnectCoordinator: ObservableObject {
    enum Phase: Equatable {
        /// Not recovering: connected, or an intentional teardown.
        case idle
        /// Actively retrying; `attempt` is the 1-based try in flight.
        case reconnecting(attempt: Int)
        /// Exhausted automatic attempts; awaiting a manual retry.
        case givenUp
    }

    @Published private(set) var phase: Phase = .idle

    /// Automatic attempts before giving up to `.givenUp`.
    static let maxAttempts = 8

    private var reconnect: (() -> Void)?
    private var isIntentionalStop: (() -> Bool)?
    private var backoff: (Int) -> TimeInterval = ReconnectCoordinator.backoffDelay

    private var backoffTask: Task<Void, Never>?
    private var attempt = 0
    private var userCancelled = false

    /// Parameterless so it can be a `@StateObject`; wired via `configure` once
    /// the clients it drives exist (their `@StateObject`s init after this one).
    init() {}

    /// - Parameters:
    ///   - reconnect: re-establishes the connection (VideoClient.reconnect),
    ///     which cascades to every stream.
    ///   - isIntentionalStop: true when the current loss is one we must not
    ///     fight -- a graceful server shutdown the user or system asked for.
    ///   - backoff: delay before the given 1-based attempt. Defaults to the
    ///     production schedule; tests inject a fast one.
    func configure(reconnect: @escaping () -> Void,
                   isIntentionalStop: @escaping () -> Bool,
                   backoff: @escaping (Int) -> TimeInterval = ReconnectCoordinator.backoffDelay) {
        self.reconnect = reconnect
        self.isIntentionalStop = isIntentionalStop
        self.backoff = backoff
    }

    /// Backoff before the given 1-based attempt: 0.5s doubling to an 8s cap.
    nonisolated static func backoffDelay(forAttempt n: Int) -> TimeInterval {
        min(0.5 * pow(2.0, Double(max(0, n - 1))), 8.0)
    }

    /// Feed every `VideoClient.connectionState` change here.
    func handleConnectionState(_ state: ConnectionState) {
        switch state {
        case .connected:
            reset()                      // recovered (or connected cleanly)
        case .error:
            noteFailureAndScheduleRetry()
        case .connecting, .disconnected:
            break                        // our own attempt, or intentional teardown
        }
    }

    /// Force an immediate attempt, resetting the backoff. Call on wake from a
    /// full system sleep.
    func handleWake() {
        userCancelled = false
        backoffTask?.cancel()
        backoffTask = nil
        attempt = 0
        noteFailureAndScheduleRetry(immediate: true)
    }

    /// Stop counting down a backoff that a sleep would only waste; wake re-kicks.
    func handleWillSleep() {
        backoffTask?.cancel()
        backoffTask = nil
    }

    /// The user asked to stop trying (Close on the overlay).
    func cancel() {
        userCancelled = true
        backoffTask?.cancel()
        backoffTask = nil
        phase = .idle
    }

    /// Manual retry from the terminal (`.givenUp`) overlay.
    func retryNow() {
        userCancelled = false
        attempt = 0
        backoffTask?.cancel()
        backoffTask = nil
        noteFailureAndScheduleRetry(immediate: true)
    }

    private func reset() {
        attempt = 0
        userCancelled = false
        backoffTask?.cancel()
        backoffTask = nil
        phase = .idle
    }

    private func noteFailureAndScheduleRetry(immediate: Bool = false) {
        guard let reconnect else { return }
        guard !userCancelled, !(isIntentionalStop?() ?? false) else { return }
        // One pending attempt at a time: a failed try emits exactly one .error,
        // and reconnect() is only issued once per scheduled attempt.
        guard backoffTask == nil else { return }

        attempt += 1
        if attempt > Self.maxAttempts {
            phase = .givenUp
            return
        }
        phase = .reconnecting(attempt: attempt)

        let delay = immediate ? 0 : backoff(attempt)
        backoffTask = Task { [weak self] in
            if delay > 0 {
                try? await Task.sleep(nanoseconds: UInt64(delay * 1_000_000_000))
            }
            guard !Task.isCancelled else { return }
            await MainActor.run {
                guard let self, !self.userCancelled else { return }
                self.backoffTask = nil
                // Re-check: a graceful shutdown may have been announced during
                // the backoff (its .stopped can arrive after the first .error).
                guard !(self.isIntentionalStop?() ?? false) else {
                    self.phase = .idle
                    return
                }
                reconnect()   // -> .connecting -> .connected (reset) or .error (next)
            }
        }
    }
}
