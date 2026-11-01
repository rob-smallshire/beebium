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

#include "connection/ReconnectPolicy.hpp"

#include <algorithm>

namespace beebium::linux_client {

std::chrono::milliseconds ReconnectPolicy::delay_for(int attempt) const {
    auto delay = FIRST_DELAY;
    for (int i = 1; i < attempt && delay < MAX_DELAY; ++i) {
        delay *= 2;
    }
    return std::min(delay, MAX_DELAY);
}

ReconnectPolicy::Step ReconnectPolicy::schedule() {
    if (attempt_ > MAX_ATTEMPTS) {
        phase_ = Phase::gave_up;
        return {};
    }
    phase_ = Phase::waiting;
    return {Step::Kind::attempt_after, delay_for(attempt_)};
}

ReconnectPolicy::Step ReconnectPolicy::on_connection_lost() {
    if (closed_ || phase_ != Phase::idle) {
        return {};
    }
    attempt_ = 1;
    return schedule();
}

void ReconnectPolicy::on_connected() {
    phase_ = Phase::idle;
    attempt_ = 0;
}

ReconnectPolicy::Step ReconnectPolicy::on_attempt_failed() {
    if (closed_ || phase_ != Phase::attempting) {
        return {};
    }
    ++attempt_;
    return schedule();
}

ReconnectPolicy::Step ReconnectPolicy::on_timer() {
    if (closed_ || phase_ != Phase::waiting) {
        return {};
    }
    phase_ = Phase::attempting;
    return {Step::Kind::attempt_now, {}};
}

void ReconnectPolicy::on_graceful_stop() {
    phase_ = Phase::idle;
    attempt_ = 0;
}

void ReconnectPolicy::on_window_closing() {
    closed_ = true;
    phase_ = Phase::idle;
    attempt_ = 0;
}

ReconnectPolicy::Step ReconnectPolicy::on_retry() {
    if (closed_ || phase_ != Phase::gave_up) {
        return {};
    }
    attempt_ = 1;
    phase_ = Phase::attempting;
    return {Step::Kind::attempt_now, {}};
}

ReconnectPolicy::Step ReconnectPolicy::on_wake(bool connection_live) {
    if (closed_ || phase_ == Phase::attempting) {
        return {};
    }
    if (phase_ == Phase::idle && !connection_live) {
        return {};  // nothing to recover: a stop, or never connected
    }
    attempt_ = 1;
    phase_ = Phase::attempting;
    return {Step::Kind::attempt_now, {}};
}

void ReconnectPolicy::on_will_sleep() {
    if (phase_ == Phase::waiting) {
        // Keep the count; the wake will try again at once.
        phase_ = Phase::gave_up;
    }
}

ConnectionVerdict connection_verdict(Liveness liveness, ReconnectPolicy::Phase phase) {
    switch (phase) {
    case ReconnectPolicy::Phase::waiting:
    case ReconnectPolicy::Phase::attempting:
        return ConnectionVerdict::reconnecting;
    case ReconnectPolicy::Phase::gave_up:
        return ConnectionVerdict::could_not_reconnect;
    case ReconnectPolicy::Phase::idle:
        break;
    }
    switch (liveness) {
    case Liveness::active: return ConnectionVerdict::live;
    case Liveness::unreachable: return ConnectionVerdict::unreachable;
    case Liveness::stopped: return ConnectionVerdict::stopped;
    case Liveness::died: return ConnectionVerdict::died;
    }
    return ConnectionVerdict::died;
}

}  // namespace beebium::linux_client
