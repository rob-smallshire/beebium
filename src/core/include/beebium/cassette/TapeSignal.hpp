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
#include <stdexcept>
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
struct TapeImageMetadata {
    std::string title;
    std::string origin;
    std::string target_machine;
    std::string instructions;
    std::vector<std::string> position_markers;
    // A sampled tape's: its rate, and for a stereo source the channel played
    // ("left" or "right"). Zero and empty otherwise: a mono source has no
    // channel to report, as a UEF or CSW has none.
    uint32_t sample_rate = 0;
    std::string channel;
};

enum class TapeKind : uint8_t { Uef, Csw, Wav, Live };

// The kind as a client sees it: "UEF", "CSW", "WAV" or "LIVE".
inline const char* tape_kind_text(TapeKind kind) {
    switch (kind) {
        case TapeKind::Csw: return "CSW";
        case TapeKind::Wav: return "WAV";
        case TapeKind::Live: return "LIVE";
        case TapeKind::Uef: break;
    }
    return "UEF";
}

// Why a tape image could not be opened. The message is a sentence naming the
// file; the kind lets a service classify it.
class TapeImageError : public std::runtime_error {
public:
    enum class Kind : uint8_t {
        CannotOpen,         // missing, or no permission
        Unreadable,         // opened, but could not be read to the end (empty, truncated, damaged)
        UnsupportedFormat,  // not a tape image Beebium reads
        InvalidImage,       // recognised, but refused (a disc chunk, a version, a value out of range)
    };
    TapeImageError(Kind kind, const std::string& sentence) : std::runtime_error(sentence), kind_(kind) {}
    Kind kind() const { return kind_; }

private:
    Kind kind_;
};

// What a tape that can be recorded on offers the deck. Recording overwrites
// forwards from one point: everything that passes under the head is replaced
// by the edges written, or by silence where none are. While a recording is in
// progress the tape is not read; end_recording() makes it readable again, from
// a fresh seek. Every call comes from the emulation thread.
class TapeRecorder {
public:
    virtual ~TapeRecorder() = default;

    // Start overwriting at `time`.
    virtual void begin_recording(TapeTime time) = 0;
    // The head has passed every time before `time`: what the tape held there
    // is gone.
    virtual void erase_before(TapeTime time) = 0;
    // An edge written at `time`, after any written before.
    virtual void record_edge(TapeTime time, bool level) = 0;
    virtual void end_recording() = 0;

    // Whether the tape has been recorded on since it was opened or last saved.
    virtual bool dirty() const = 0;
    // The tape as a whole file, ready to replace the one it came from. Not
    // during a recording.
    virtual std::vector<uint8_t> encode() = 0;
    // The file now holds what encode() gave.
    virtual void mark_clean() = 0;
};

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

    virtual const TapeImageMetadata& metadata() const = 0;

    // The tape's recorder, or null for a tape Beebium only reads.
    virtual TapeRecorder* recorder() { return nullptr; }
    bool recordable() { return recorder() != nullptr; }
};

// Convert seconds to tape time.
inline TapeTime seconds_to_tape_time(double seconds) {
    return static_cast<TapeTime>(seconds * ULA_CLOCK_HZ);
}

}  // namespace beebium
