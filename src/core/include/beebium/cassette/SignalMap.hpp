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

#pragma once

#include "TapeSignal.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace beebium {

// The signal map: where a tape carries signal, one level a second of tape, 0
// (silent throughout) to 255 (signal throughout), the fraction of that second
// lying within a run of edges. Its length is the tape's duration as reported
// (to the millisecond) in seconds, rounded up; the last second's fraction is of
// the part of it the tape holds.
//
// A run is a sequence of edges each less than SIGNAL_RUN_SPACING after the
// last, from its first edge to its last; a Gap item, or a longer spacing, ends
// one. Every tone a tape carries is a run, carrier included: carrier is signal,
// since a person looking for a recording looks for where its leader begins. A
// WAV is walked through the front end like any tape, so its silence is what
// the comparator's hysteresis rejects, with no amplitude threshold of its own.
// The map is computed from the TapeSignal the deck would play, so every kind
// of tape is treated alike.

// A Decision: 4 ms of ULA clocks. A 300 baud zero's half-cycle is 1.67 ms, so
// every legitimate tone is a run.
inline constexpr TapeTime SIGNAL_RUN_SPACING = 4 * ULA_CLOCK_NUMERATOR / (1000 * ULA_CLOCK_DENOMINATOR);

// Walk `tape` from its start to its end and return its levels, or nothing if
// `cancelled` became true first (it is read once an item).
std::optional<std::vector<uint8_t>> compute_signal_map(TapeSignal& tape,
                                                       const std::atomic<bool>* cancelled = nullptr);

// Computes a deck's signal map on a worker thread of its own, so the
// emulation thread only posts a job. The worker starts with the first job and
// lives until the mapper is destroyed. The latest job wins: posting one, or
// clearing, cancels a walk in progress, and a walk superseded never
// publishes. A walk notices cancellation within one item of its tape, and no
// item costs more than a block of a WAV's samples. The destructor cancels the
// walk and joins the worker, so no walk outlives the mapper; a job's opener
// owns everything it reads (a file path, or bytes), never the deck's tape.
class SignalMapper {
public:
    // Opens a TapeSignal of its own on the tape to be mapped, on the worker.
    using Opener = std::function<std::unique_ptr<TapeSignal>()>;

    struct Map {
        bool ready = false;            // a map has been published for the current tape
        std::vector<uint8_t> levels;   // one a second, when ready
        uint32_t generation = 0;       // the number of maps published, ever
    };

    SignalMapper() = default;
    SignalMapper(const SignalMapper&) = delete;
    SignalMapper& operator=(const SignalMapper&) = delete;
    ~SignalMapper();

    // Map the tape `open` opens, superseding any map or job before. From any thread.
    void map(Opener open);
    // No map until the next map(), cancelling any job. From any thread.
    void clear();

    Map snapshot() const;
    bool ready() const { return ready_.load(std::memory_order_acquire); }
    uint32_t generation() const { return generation_.load(std::memory_order_acquire); }

private:
    void run();

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    bool stopping_ = false;             // under mutex_
    std::optional<Opener> pending_;     // under mutex_
    uint64_t job_ = 0;                  // the latest job's number, under mutex_
    std::vector<uint8_t> levels_;       // under mutex_
    std::atomic<bool> cancelled_{false};
    std::atomic<bool> ready_{false};
    std::atomic<uint32_t> generation_{0};
};

}  // namespace beebium
