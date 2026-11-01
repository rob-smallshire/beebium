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

#ifndef BEEBIUM_LINUX_CONNECTION_LIVENESS_HPP
#define BEEBIUM_LINUX_CONNECTION_LIVENESS_HPP

#include <chrono>
#include <optional>

namespace beebium::linux_client {

// Whether the server is there (client specification 4.3; lifecycle-
// management.md section 1). Derived from SystemService.WatchServerStatus
// alone, never from frames, which stop while a machine is paused.
enum class Liveness {
    active,       // events, heartbeats included, are arriving
    unreachable,  // heartbeats stopped with the stream still open; recoverable
    stopped,      // the stream ended after the server announced its shutdown; terminal
    died,         // the stream ended with no announcement; terminal
};

const char* liveness_name(Liveness liveness);

// The liveness state machine, with time supplied by the caller so it can be
// tested without waiting.
//
//  active      <- any event (including from unreachable)
//  unreachable <- the watchdog finds no event for `watchdog`, from active only
//  stopped     <- the stream ends, not by us, after a shutting-down event
//  died        <- the stream ends, not by us, with no announcement
//
// A stream we cancelled ourselves (a close or a reconnect) changes nothing.
// stopped and died stay until reset(), for a new connection.
class LivenessTracker {
public:
    using Clock = std::chrono::steady_clock;

    // macOS waits 2 s over the server's 0.5 s heartbeat: a few missed beats
    // tolerated, a frozen server noticed within about two seconds.
    static constexpr std::chrono::milliseconds DEFAULT_WATCHDOG{2000};

    explicit LivenessTracker(std::chrono::milliseconds watchdog = DEFAULT_WATCHDOG);

    // A status event of any kind arrived.
    void on_event(Clock::time_point now, bool announces_shutdown);

    // Time passes; returns true if the state changed.
    bool on_tick(Clock::time_point now);

    // The status stream ended.
    void on_stream_end(bool cancelled_by_client);

    // A new connection: active, nothing announced, the watchdog unarmed until
    // the first event.
    void reset();

    Liveness state() const { return state_; }
    bool terminal() const { return state_ == Liveness::stopped || state_ == Liveness::died; }

private:
    std::chrono::milliseconds watchdog_;
    Liveness state_ = Liveness::active;
    bool shutdown_announced_ = false;
    std::optional<Clock::time_point> last_event_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_CONNECTION_LIVENESS_HPP
