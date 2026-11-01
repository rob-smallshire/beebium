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

#include "connection/Liveness.hpp"

namespace beebium::linux_client {

const char* liveness_name(Liveness liveness) {
    switch (liveness) {
    case Liveness::active: return "active";
    case Liveness::unreachable: return "unreachable";
    case Liveness::stopped: return "stopped";
    case Liveness::died: return "died";
    }
    return "unknown";
}

LivenessTracker::LivenessTracker(std::chrono::milliseconds watchdog) : watchdog_(watchdog) {}

void LivenessTracker::on_event(Clock::time_point now, bool announces_shutdown) {
    if (terminal()) {
        return;
    }
    last_event_ = now;
    shutdown_announced_ = shutdown_announced_ || announces_shutdown;
    state_ = Liveness::active;
}

bool LivenessTracker::on_tick(Clock::time_point now) {
    if (state_ != Liveness::active || !last_event_) {
        return false;
    }
    if (now - *last_event_ > watchdog_) {
        state_ = Liveness::unreachable;
        return true;
    }
    return false;
}

void LivenessTracker::on_stream_end(bool cancelled_by_client) {
    if (cancelled_by_client || terminal()) {
        return;
    }
    state_ = shutdown_announced_ ? Liveness::stopped : Liveness::died;
}

void LivenessTracker::reset() {
    state_ = Liveness::active;
    shutdown_announced_ = false;
    last_event_.reset();
}

}  // namespace beebium::linux_client
