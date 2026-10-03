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

// Rate limit for re-publishing the _beebium._tcp announcement (#153).
//
// A machine's mDNS instance name is its rendered name, and an instance cannot
// be renamed in place: re-announcing withdraws the old registration and
// probes and announces a new one, a burst of multicast traffic over the next
// few seconds. A placeholder whose value flaps (a station renumbered back and
// forth) would otherwise churn the responder; see the responder-saturation
// note in docs/networking.md. Requests arriving within the interval coalesce
// into one re-announcement of the latest name when it ends.
//
// Not thread-safe: the owner serialises access.

#ifndef BEEBIUM_SERVICE_REANNOUNCE_LIMITER_HPP
#define BEEBIUM_SERVICE_REANNOUNCE_LIMITER_HPP

#include <chrono>
#include <optional>

namespace beebium::service {

class ReannounceLimiter {
public:
    using Clock = std::chrono::steady_clock;

    explicit ReannounceLimiter(Clock::duration min_interval) : min_interval_(min_interval) {}

    // A re-announcement is wanted.
    void request() { pending_ = true; }

    // Whether to re-announce now: a request is pending and the last
    // re-announcement was at least the interval ago. If so, records it.
    bool take(Clock::time_point now) {
        if (!pending_) return false;
        if (last_ && now - *last_ < min_interval_) return false;
        pending_ = false;
        last_ = now;
        return true;
    }

    // A re-announcement was made directly (a user's rename, which is
    // deliberate and rare and so not delayed); it satisfies any pending
    // request and starts the interval.
    void note_published(Clock::time_point now) {
        pending_ = false;
        last_ = now;
    }

    bool pending() const { return pending_; }

private:
    Clock::duration min_interval_;
    std::optional<Clock::time_point> last_;
    bool pending_ = false;
};

}  // namespace beebium::service

#endif  // BEEBIUM_SERVICE_REANNOUNCE_LIMITER_HPP
