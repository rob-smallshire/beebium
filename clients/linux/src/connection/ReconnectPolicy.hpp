// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_LINUX_CONNECTION_RECONNECT_POLICY_HPP
#define BEEBIUM_LINUX_CONNECTION_RECONNECT_POLICY_HPP

#include "connection/Liveness.hpp"

#include <chrono>
#include <optional>

namespace beebium::linux_client {

// When to reconnect, and for how long to keep trying (client specification
// 4.4; frontend-sleep-wake.md). Pure: the caller owns the timer and the
// reconnect itself, and tells the policy what happened.
//
//  - A connection that dies starts recovery: try again after 0.5 s, then
//    doubling to an 8 s cap, up to eight attempts, then give up to a
//    terminal state the user can retry from.
//  - A graceful stop is never fought: recovery ends.
//  - Waking from full system sleep tries at once and resets the backoff;
//    going to sleep cancels the pending attempt.
//  - Closing the window cancels recovery before anything else.
class ReconnectPolicy {
public:
    enum class Phase {
        idle,         // nothing to recover
        waiting,      // an attempt is scheduled
        attempting,   // a reconnect is in progress
        gave_up,      // attempts exhausted; Retry starts again
    };

    // What the caller does next: nothing, start an attempt now, or start one
    // after a delay (replacing any pending one).
    struct Step {
        enum class Kind { none, attempt_now, attempt_after } kind = Kind::none;
        std::chrono::milliseconds delay{0};
    };

    static constexpr int MAX_ATTEMPTS = 8;
    static constexpr std::chrono::milliseconds FIRST_DELAY{500};
    static constexpr std::chrono::milliseconds MAX_DELAY{8000};

    // The connection was lost for good (died): start recovering.
    Step on_connection_lost();
    // The connection was established, first time or again: recovery done.
    void on_connected();
    // An attempt did not connect: schedule the next, or give up.
    Step on_attempt_failed();
    // The scheduled delay elapsed: the caller now attempts.
    Step on_timer();
    // The server announced its shutdown: an intentional stop, never fought.
    void on_graceful_stop();
    // The window is closing: recovery ends before teardown.
    void on_window_closing();
    // The user asked to try again from the gave-up state.
    Step on_retry();
    // Woke from full system sleep: the connection is very likely dead, so
    // try now, with the backoff reset, whether or not a loss was noticed.
    Step on_wake(bool connection_live);
    // Going to full system sleep: a pending attempt would only run on wake.
    void on_will_sleep();

    Phase phase() const { return phase_; }
    int attempt() const { return attempt_; }  // 1-based, the one in progress or next

private:
    std::chrono::milliseconds delay_for(int attempt) const;
    Step schedule();

    Phase phase_ = Phase::idle;
    int attempt_ = 0;
    bool closed_ = false;
};

// What a machine window shows about its connection: one verdict combining
// liveness and recovery, so every part of the window agrees (4.3, 4.4;
// lifecycle-management.md "Sidebars and cached server state").
enum class ConnectionVerdict {
    live,                  // nothing shown
    unreachable,           // spinner: heartbeats stopped, waiting for them
    reconnecting,          // spinner with the attempt count
    could_not_reconnect,   // terminal until Retry: Retry and Close
    stopped,               // the emulator stopped: Close
    died,                  // the emulator stopped unexpectedly: Close
};

const char* verdict_name(ConnectionVerdict verdict);

// An active recovery outranks the raw liveness: a stream that died reads as
// reconnecting while recovery is trying.
ConnectionVerdict connection_verdict(Liveness liveness, ReconnectPolicy::Phase phase);

// Whether the window's panels show and drive current server state.
inline bool is_live(ConnectionVerdict verdict) {
    return verdict == ConnectionVerdict::live;
}

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_CONNECTION_RECONNECT_POLICY_HPP
