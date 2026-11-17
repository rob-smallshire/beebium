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

#include "beebium/cassette/SignalMap.hpp"

#include <algorithm>
#include <exception>
#include <iostream>

namespace beebium {

namespace {

// The tape time at which second `s` begins, exactly.
TapeTime second_start(uint64_t s) { return s * ULA_CLOCK_NUMERATOR / ULA_CLOCK_DENOMINATOR; }

// The second tape time `t` falls in: the last whose start is at or before it.
// (Both conversions floor, so the quotient can be one short at a boundary.)
uint64_t second_of(TapeTime t) {
    uint64_t s = t * ULA_CLOCK_DENOMINATOR / ULA_CLOCK_NUMERATOR;
    while (second_start(s + 1) <= t) ++s;
    return s;
}

}  // namespace

std::optional<std::vector<uint8_t>> compute_signal_map(TapeSignal& tape, const std::atomic<bool>* cancelled) {
    // Clocks of signal in each second, grown as the walk goes.
    std::vector<uint64_t> covered;
    auto add_run = [&](TapeTime from, TapeTime to) {
        while (from < to) {
            const uint64_t s = second_of(from);
            const TapeTime until = std::min(to, second_start(s + 1));
            if (covered.size() <= s) covered.resize(s + 1, 0);
            covered[s] += until - from;
            from = until;
        }
    };

    tape.rewind();
    bool in_run = false;
    TapeTime run_start = 0;
    TapeTime last_edge = 0;
    TapeTime end = 0;
    for (;;) {
        if (cancelled != nullptr && cancelled->load(std::memory_order_relaxed)) return std::nullopt;
        const TapeItem item = tape.next();
        if (item.type == TapeItem::Type::Edge) {
            if (!in_run || item.time - last_edge >= SIGNAL_RUN_SPACING) {
                if (in_run) add_run(run_start, last_edge);
                run_start = item.time;
                in_run = true;
            }
            last_edge = item.time;
        } else {
            if (in_run) add_run(run_start, last_edge);
            in_run = false;
            if (item.type == TapeItem::Type::End) {
                end = item.time;
                break;
            }
        }
    }

    // The duration as the deck reports it, to the millisecond, rounded up to
    // seconds: a UEF's float gap (&0116) can leave a tape a few clocks over a
    // whole second, which would otherwise add a second the tape barely holds.
    const TapeTime length = tape.duration().value_or(end);
    const uint64_t milliseconds = (length * ULA_CLOCK_DENOMINATOR * 1000 + ULA_CLOCK_NUMERATOR / 2) / ULA_CLOCK_NUMERATOR;
    const uint64_t seconds = (milliseconds + 999) / 1000;
    std::vector<uint8_t> levels(seconds, 0);
    for (uint64_t s = 0; s < seconds && s < covered.size(); ++s) {
        const TapeTime span = std::min(second_start(s + 1), length) - second_start(s);
        if (span == 0) continue;
        const uint64_t level = (std::min(covered[s], span) * 255 + span / 2) / span;
        levels[s] = static_cast<uint8_t>(level);
    }
    return levels;
}

SignalMapper::~SignalMapper() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        pending_.reset();
        cancelled_.store(true, std::memory_order_relaxed);
    }
    wake_.notify_one();
    if (worker_.joinable()) worker_.join();
}

void SignalMapper::map(Opener open) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++job_;
        pending_ = std::move(open);
        levels_.clear();
        ready_.store(false, std::memory_order_release);
        cancelled_.store(true, std::memory_order_relaxed);
        if (!worker_.joinable()) worker_ = std::thread([this] { run(); });
    }
    wake_.notify_one();
}

void SignalMapper::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++job_;
    pending_.reset();
    levels_.clear();
    ready_.store(false, std::memory_order_release);
    cancelled_.store(true, std::memory_order_relaxed);
}

SignalMapper::Map SignalMapper::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Map map;
    map.ready = ready_.load(std::memory_order_relaxed);
    if (map.ready) map.levels = levels_;
    map.generation = generation_.load(std::memory_order_relaxed);
    return map;
}

void SignalMapper::run() {
    for (;;) {
        Opener open;
        uint64_t job = 0;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || pending_.has_value(); });
            if (stopping_) return;
            open = std::move(*pending_);
            pending_.reset();
            job = job_;
            cancelled_.store(false, std::memory_order_relaxed);
        }

        std::optional<std::vector<uint8_t>> levels;
        try {
            const std::unique_ptr<TapeSignal> tape = open();
            if (tape) levels = compute_signal_map(*tape, &cancelled_);
        } catch (const std::exception& e) {
            std::cerr << e.what() << " So the tape has no signal map.\n";
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (levels && job == job_ && !stopping_) {
            levels_ = std::move(*levels);
            ready_.store(true, std::memory_order_release);
            generation_.fetch_add(1, std::memory_order_acq_rel);
        }
    }
}

}  // namespace beebium
