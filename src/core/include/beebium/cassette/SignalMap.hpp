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
#include <span>
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

// While a deck records, its map is kept current in the head's wake and
// republished at most once per this much tape time (a Decision).
inline constexpr TapeTime SIGNAL_MAP_RECORDING_PUBLISH_CLOCKS = ULA_CLOCK_NUMERATOR / ULA_CLOCK_DENOMINATOR;

// The tape time at which second `s` of the map begins, exactly.
inline TapeTime signal_map_second_start(uint64_t s) { return s * ULA_CLOCK_NUMERATOR / ULA_CLOCK_DENOMINATOR; }

// The second of the map tape time `t` falls in: the last whose start is at or
// before it. (Both conversions floor, so the quotient can be one short.)
inline uint64_t signal_map_second_of(TapeTime t) {
    uint64_t s = t * ULA_CLOCK_DENOMINATOR / ULA_CLOCK_NUMERATOR;
    while (signal_map_second_start(s + 1) <= t) ++s;
    return s;
}

// The append point: where to start a recording that follows the last one on a
// tape. The end of the last second of the map with a non-zero level, plus a
// margin of APPEND_MARGIN_SECONDS (a Decision), so the next leader starts 2 to
// 3 s after the last edge; 0 for a map that is silent throughout. Returned as
// a second of the map, which can lie past the tape's end.
inline constexpr uint64_t APPEND_MARGIN_SECONDS = 2;

inline uint64_t signal_map_append_second(std::span<const uint8_t> levels) {
    for (size_t s = levels.size(); s > 0; --s) {
        if (levels[s - 1] != 0) return s + APPEND_MARGIN_SECONDS;
    }
    return 0;
}

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

    // Map the tape `open` opens, superseding any job before. The map before
    // stays published until the new one replaces it if `keep_current` (the
    // same tape, newly recorded on); otherwise there is none until then.
    // From any thread.
    void map(Opener open, bool keep_current = false);
    // Write `levels`, from second `first` on, into the published map: a
    // recording's wake. A new generation is published unless `quietly` (a
    // walk that will publish one is about to follow). Nothing if no map is
    // ready. From any thread; holds the mutex for the copy.
    void update(size_t first, std::span<const uint8_t> levels, bool quietly = false);
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
