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

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace beebium {

// Tape time: a count of Serial ULA clocks (16 MHz / 13) from the start of the
// tape. The deck advances it one ULA clock at a time.
using TapeTime = uint64_t;

// The ULA clock rate as an exact ratio, for converting seconds and sample
// counts to tape time without drift.
inline constexpr uint64_t ULA_CLOCK_NUMERATOR = 16'000'000;
inline constexpr uint64_t ULA_CLOCK_DENOMINATOR = 13;
inline constexpr double ULA_CLOCK_HZ =
    static_cast<double>(ULA_CLOCK_NUMERATOR) / static_cast<double>(ULA_CLOCK_DENOMINATOR);

// One item of a tape signal, in tape-time order.
//
//   Edge: a transition of the squared cassette signal at `time`; `level` is
//         the signal's level after it (true = high). The Serial ULA's data
//         separator responds to edges of either sense; the level is there for
//         tests and for a future recorder.
//   Gap:  silence: no edges before `time`.
//   End:  the end of the tape; `time` is its length.
struct TapeItem {
    enum class Type : uint8_t { Edge, Gap, End };
    Type type = Type::End;
    TapeTime time = 0;
    bool level = false;

    static TapeItem edge(TapeTime t, bool level) { return {Type::Edge, t, level}; }
    static TapeItem gap(TapeTime until) { return {Type::Gap, until, false}; }
    static TapeItem end(TapeTime t) { return {Type::End, t, false}; }
};

// What a tape image says about itself, where it says anything.
struct TapeMetadata {
    std::string title;
    std::string origin;
    std::string target_machine;
    std::string instructions;
    std::vector<std::string> position_markers;
};

enum class TapeKind : uint8_t { Uef, Csw };

// The seam between a tape and the deck: a source of edges in tape time.
//
// A source yields items in tape-time order and never yields a time earlier
// than the last it yielded. Every call comes from the emulation thread; a file
// source decodes ahead on that thread and holds no lock.
class TapeSignal {
public:
    virtual ~TapeSignal() = default;

    // The next item. After End, End again.
    virtual TapeItem next() = 0;

    // Reposition so that the next item is the first at or after `time`.
    virtual void seek(TapeTime time) = 0;

    void rewind() { seek(0); }

    // The tape's length in tape time, when known.
    virtual std::optional<TapeTime> duration() const = 0;

    virtual TapeKind kind() const = 0;

    virtual const TapeMetadata& metadata() const = 0;
};

// Convert seconds to tape time.
inline TapeTime seconds_to_tape_time(double seconds) {
    return static_cast<TapeTime>(seconds * ULA_CLOCK_HZ);
}

}  // namespace beebium
