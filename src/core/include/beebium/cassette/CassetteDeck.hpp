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

#include "SignalMap.hpp"
#include "TapeImage.hpp"
#include "TapeSignal.hpp"
#include "TapeSink.hpp"

#include <algorithm>
#include <atomic>
#include <concepts>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace beebium {

// The cassette deck: the user's transport around one tape.
//
// Play, stop and position belong to the person; the motor relay belongs to the
// emulated machine. Tape time advances one ULA clock at a time only while the
// deck is playing and the relay is closed: the relay stops the tape at once,
// with no inertia. Inserting leaves the deck stopped, seeking is instant, and
// the end of the tape stops it. Break and power cycle do not touch the deck.
//
// Rewinding and fast-forwarding wind the tape at WIND_SPEED times real speed,
// backwards or forwards, whether or not the relay is closed, and the tape's
// edges reach nobody: a wound tape is off the head. A wind ends on stop, play,
// seek, eject, or at either end of the tape, where the deck stops.
//
// A live tape (a client's audio input) is not gated by the relay: a deck
// without a remote lead keeps playing, and the real ULA keeps listening. It is
// paced by emulated time, a sample a sample period, whether the deck plays or
// not (stopped, the samples are taken and go nowhere, so its ring does not
// fill); only a playing deck passes its edges on. It cannot wind, seek or be
// recorded on, and its position counts the samples taken since it began.
//
// Recording is a transport like playing: the tape moves only while the relay
// is closed, and every clock that passes under the head replaces what the
// tape held there with what the Serial ULA's tone synthesiser sends, an edge
// or silence. The deck is the synthesiser's sink while it records. Only a tape
// with a recorder (a UEF) can be recorded on. Stop, play, seek, a wind, eject
// and the end of the tape end a recording. A tape recorded on is written back
// to its file, whole and atomically (written beside it, then renamed over it),
// when a recording ends, on eject, on flush() and when the deck is destroyed,
// and only if it has been recorded on since it was last written.
//
// The Serial ULA clocks the deck on the emulation thread and takes the edges
// that fall on each clock. CassetteService drives the transport with the
// emulation paused, and reads the deck's state without pausing it through
// the atomics the deck publishes: presence, transport, relay and a tape
// generation when they change, and the position whenever the transport moves
// and every POSITION_PUBLISH_CLOCKS while playing, so the clock path pays a
// counter rather than a store a clock.
//
// The deck keeps the tape's signal map (SignalMap.hpp), computed by its own
// worker from a TapeSignal opened afresh on the same image, never from the
// tape the emulation thread plays: on insert, from the tape's file (a stereo
// WAV's channel as played), or from a UEF's encoded image if it has no file;
// when a recording ends, from the image the deck encodes then, the bytes it
// writes back. A live tape, and a tape that Beebium only reads inserted with no
// file, have none. The emulation thread only posts the job. While recording,
// the deck keeps the map current in the head's wake itself, from the edges and
// clocks it records: the same run rule, applied to each second as the head
// passes it (a completed second is final, the one under the head is the
// fraction of its passed part in a run, seconds ahead keep their levels), and
// republished at most once per SIGNAL_MAP_RECORDING_PUBLISH_CLOCKS of tape
// time. When the recording ends, the walk of the image written back replaces
// it, the wake's map staying published until then.
class CassetteDeck final : public TapeSink {
public:
    enum class Transport : uint8_t { Stopped, Playing, Rewinding, FastForwarding, Recording };

    // Tape time a wind covers per ULA clock.
    static constexpr TapeTime WIND_SPEED = 30;

    // While playing, the published position is at most this many ULA clocks
    // (3.3 ms) behind the deck's.
    static constexpr uint32_t POSITION_PUBLISH_CLOCKS = 4096;

    // What the deck publishes, readable from any thread.
    struct Published {
        bool has_tape = false;
        Transport transport = Transport::Stopped;
        TapeTime position = 0;
        bool relay_closed = false;
        uint64_t tape_generation = 0;  // bumped on every insert and eject
        uint64_t sequence = 0;         // bumped on every change but the position's
        bool signal_map_ready = false;
        uint32_t signal_map_generation = 0;  // bumped each time a signal map is published
    };

    CassetteDeck() = default;
    CassetteDeck(const CassetteDeck&) = delete;
    CassetteDeck& operator=(const CassetteDeck&) = delete;
    ~CassetteDeck() override { flush(); }

    // Throws std::runtime_error with a sentence if a tape is already in.
    // `name` is what the tape is called in messages; `url` is its source as
    // given, for reporting; `filepath` is the file a recording is written back
    // to, or empty for none.
    void insert(std::unique_ptr<TapeSignal> tape, std::string name, std::string url = {},
                std::filesystem::path filepath = {}) {
        if (tape_) {
            throw std::runtime_error("The deck already holds " + name_ +
                                     "; eject it before inserting another tape.");
        }
        tape_ = std::move(tape);
        live_ = tape_->kind() == TapeKind::Live;
        name_ = std::move(name);
        url_ = std::move(url);
        filepath_ = std::move(filepath);
        configured_ = false;
        transport_ = Transport::Stopped;
        position_ = 0;
        tape_->rewind();
        pending_ = tape_->next();
        map_inserted_tape();
        published_tape_generation_.fetch_add(1, std::memory_order_relaxed);
        publish();
    }

    void eject() {
        end_recording();
        flush();
        live_ = false;
        signal_map_.clear();
        tape_.reset();
        name_.clear();
        url_.clear();
        filepath_.clear();
        configured_ = false;
        transport_ = Transport::Stopped;
        position_ = 0;
        pending_ = TapeItem::end(0);
        published_tape_generation_.fetch_add(1, std::memory_order_relaxed);
        publish();
    }

    void play() {
        end_recording();
        end_wind();
        if (tape_ && !at_end()) transport_ = Transport::Playing;
        publish();
    }

    void stop() {
        end_recording();
        end_wind();
        transport_ = Transport::Stopped;
        publish();
    }

    // Wind backwards to the start, where the deck stops.
    void rewind() {
        if (!tape_ || live_) return;
        end_recording();
        end_wind();
        transport_ = position_ > 0 ? Transport::Rewinding : Transport::Stopped;
        publish();
    }

    // Wind forwards to the end, where the deck stops.
    void fast_forward() {
        if (!tape_ || live_) return;
        end_recording();
        end_wind();
        transport_ = position_ < end_time() ? Transport::FastForwarding : Transport::Stopped;
        publish();
    }

    bool winding() const {
        return transport_ == Transport::Rewinding || transport_ == Transport::FastForwarding;
    }

    // Reposition instantly; a time beyond the end lands at the end and stops.
    // A seek ends a wind; it leaves a playing deck playing.
    void seek(TapeTime time) {
        if (!tape_ || live_) return;
        end_recording();
        if (winding()) transport_ = Transport::Stopped;
        const auto length = tape_->duration();
        if (length && time >= *length) {
            time = *length;
            transport_ = Transport::Stopped;
        }
        tape_->seek(time);
        position_ = time;
        pending_ = tape_->next();
        publish();
    }

    // Press record: from here, while the relay is closed, the tape moves and
    // what passes under the head is replaced by what the machine sends. At the
    // end of the tape the deck stays stopped. Throws std::runtime_error with a
    // sentence if the deck is empty or the tape cannot be recorded on.
    void record() {
        if (!tape_) throw std::runtime_error("There is no tape in the deck.");
        if (const auto why = why_not_recordable()) throw std::runtime_error(*why);
        if (transport_ == Transport::Recording) return;
        end_wind();
        if (position_ >= end_time()) {
            transport_ = Transport::Stopped;
        } else {
            transport_ = Transport::Recording;
            tape_->recorder()->begin_recording(position_);
            begin_wake();
        }
        publish();
    }

    bool recording() const { return transport_ == Transport::Recording; }

    // Whether the deck holds a live tape.
    bool live() const { return live_; }

    // Whether the tape in the deck can be recorded on: a UEF whose file the
    // deck can write back to, the file and the folder it is in both writable.
    // (A tape held only in memory has nowhere to keep a recording.)
    bool recordable() const { return tape_ != nullptr && !why_not_recordable(); }

    // Why the tape in the deck cannot be recorded on, as a sentence, or
    // nothing when it can (or the deck is empty). Asks the file system, so it
    // is for a key press or a status, never the clock path.
    std::optional<std::string> why_not_recordable() const {
        if (!tape_) return std::nullopt;
        if (live_) return "The live input " + name_ + " cannot be recorded on.";
        if (tape_->recorder() == nullptr) {
            if (tape_->kind() != TapeKind::Uef) {
                return "The tape " + name_ + " is a " + tape_kind_text(tape_->kind()) +
                       " image, and Beebium records only on UEF tapes.";
            }
            return "The tape " + name_ + " changes its base frequency (UEF chunk &0113), which Beebium "
                   "cannot yet write back, so it does not record on it.";
        }
        if (filepath_.empty()) {
            return "The tape " + name_ + " is held only in memory, with no file to keep a recording in, "
                   "so Beebium does not record on it.";
        }
        if (!can_write(filepath_) || !can_write(filepath_.has_parent_path() ? filepath_.parent_path()
                                                                            : std::filesystem::path("."))) {
            return "Beebium cannot write the tape " + name_ + " or the folder it is in, so it does not "
                   "record on it; record on a copy you can write.";
        }
        return std::nullopt;
    }

    // One ULA clock with the relay as the machine has it. Returns true if an
    // edge of the tape falls on this clock.
    bool clock(bool relay_closed) {
        if (relay_closed != relay_closed_) {
            relay_closed_ = relay_closed;
            published_relay_.store(relay_closed, std::memory_order_relaxed);
            published_position_.store(position_, std::memory_order_relaxed);
            published_sequence_.fetch_add(1, std::memory_order_release);
        }
        if (live_) {
            const bool edge = advance();
            return transport_ == Transport::Playing && edge;
        }
        if (transport_ == Transport::Playing) {
            return relay_closed && advance();
        }
        if (transport_ == Transport::Recording) {
            record_clock(relay_closed);
            return false;
        }
        if (winding()) wind();
        return false;
    }

    // TapeSink: the tone synthesiser's output, on the clock the deck has just
    // run. Written at the head while recording with the relay closed.
    void edge(TapeTime, bool level) override {
        if (!head_writing_) return;
        tape_->recorder()->record_edge(head_time_, level);
        wake_edge(head_time_);
    }
    void silence_until(TapeTime) override {}

    // Write a tape recorded on back to its file, if it has been recorded on
    // since it was last written; a recording in progress carries on. Returns
    // false, having logged a sentence, if the file could not be written; the
    // tape then stays unwritten.
    bool flush() {
        if (!tape_ || filepath_.empty()) return true;
        TapeRecorder* recorder = tape_->recorder();
        if (recorder == nullptr || !recorder->dirty()) return true;
        const bool was_recording = transport_ == Transport::Recording;
        if (was_recording) recorder->end_recording();
        const std::vector<uint8_t> bytes = recorder->encode();
        if (was_recording) recorder->begin_recording(position_);
        return write_back(bytes);
    }

    // The published state, from any thread.
    Published published() const {
        Published p;
        p.sequence = published_sequence_.load(std::memory_order_acquire);
        p.has_tape = published_has_tape_.load(std::memory_order_relaxed);
        p.transport = published_transport_.load(std::memory_order_relaxed);
        p.position = published_position_.load(std::memory_order_relaxed);
        p.relay_closed = published_relay_.load(std::memory_order_relaxed);
        p.tape_generation = published_tape_generation_.load(std::memory_order_relaxed);
        p.signal_map_ready = signal_map_.ready();
        p.signal_map_generation = signal_map_.generation();
        return p;
    }

    TapeTime position() const { return position_; }
    std::optional<TapeTime> duration() const {
        return tape_ ? tape_->duration() : std::nullopt;
    }
    Transport transport() const { return transport_; }
    bool has_tape() const { return tape_ != nullptr; }
    const std::string& name() const { return name_; }
    const std::string& url() const { return url_; }
    const std::filesystem::path& filepath() const { return filepath_; }

    // The tape's file has moved (its machine's directory was renamed): write
    // back there from now on.
    void relocate(std::filesystem::path filepath, std::string url) {
        filepath_ = std::move(filepath);
        url_ = std::move(url);
    }

    // Whether the tape in the deck is the one the machine's configuration
    // names (inserted at launch), rather than one inserted while it runs. A
    // saved machine records the position of its configured tape only.
    bool configured() const { return tape_ != nullptr && configured_; }
    void set_configured(bool configured) { configured_ = configured; }
    std::optional<TapeKind> kind() const {
        return tape_ ? std::optional<TapeKind>(tape_->kind()) : std::nullopt;
    }
    const TapeImageMetadata& metadata() const {
        static const TapeImageMetadata none;
        return tape_ ? tape_->metadata() : none;
    }
    // The tape's signal map, from any thread.
    SignalMapper::Map signal_map() const { return signal_map_.snapshot(); }

    // Whether the tape is silent at the current position.
    bool in_gap() const { return pending_.type == TapeItem::Type::Gap; }

private:
    // One ULA clock of recording: with the relay closed the head erases what
    // the tape held at this clock and writes what edge() delivers.
    void record_clock(bool relay_closed) {
        head_writing_ = false;
        if (!relay_closed) return;
        if (position_ >= end_time()) {
            end_recording();
            publish();
            return;
        }
        if (position_ >= wake_second_end_) roll_wake();
        if (position_ >= wake_next_publish_) publish_wake();
        head_time_ = position_;
        head_writing_ = true;
        tape_->recorder()->erase_before(position_ + 1);
        ++position_;
        if (++clocks_since_publish_ >= POSITION_PUBLISH_CLOCKS) {
            clocks_since_publish_ = 0;
            published_position_.store(position_, std::memory_order_relaxed);
        }
    }

    // Leave the Recording transport, stopped, write the tape back, and map
    // it again from the same encoded image.
    void end_recording() {
        if (transport_ != Transport::Recording) return;
        head_writing_ = false;
        TapeRecorder* recorder = tape_->recorder();
        recorder->end_recording();
        transport_ = Transport::Stopped;
        tape_->seek(position_);
        pending_ = tape_->next();
        auto bytes = std::make_shared<const std::vector<uint8_t>>(recorder->encode());
        if (!filepath_.empty() && recorder->dirty()) write_back(*bytes);
        map_image(std::move(bytes), true);
    }

    // --- The signal map in the head's wake, while recording ---

    void begin_wake() {
        wake_first_second_ = signal_map_second_of(position_);
        wake_second_ = wake_first_second_;
        wake_second_end_ = signal_map_second_start(wake_second_ + 1);
        wake_from_ = position_;
        wake_covered_ = 0;
        wake_in_run_ = false;
        wake_levels_.clear();
        wake_next_publish_ = position_ + SIGNAL_MAP_RECORDING_PUBLISH_CLOCKS;
    }

    // An edge written at `time`: a run goes on while edges come less than
    // SIGNAL_RUN_SPACING apart, and the second under the head gains what it
    // covers since the last.
    void wake_edge(TapeTime time) {
        if (wake_in_run_ && time - wake_last_edge_ < SIGNAL_RUN_SPACING) {
            wake_covered_ += time - std::max(wake_last_edge_, wake_from_);
        }
        wake_in_run_ = true;
        wake_last_edge_ = time;
    }

    // The clocks of the second under the head in a run, up to `until`.
    TapeTime wake_covered_until(TapeTime until) const {
        TapeTime covered = wake_covered_;
        if (wake_in_run_ && until - wake_last_edge_ < SIGNAL_RUN_SPACING) {
            covered += until - std::max(wake_last_edge_, wake_from_);
        }
        return covered;
    }

    static uint8_t wake_level(TapeTime covered, TapeTime span) {
        if (span == 0) return 0;
        return static_cast<uint8_t>((std::min(covered, span) * 255 + span / 2) / span);
    }

    // The head has finished a second: its level is final.
    void roll_wake() {
        wake_levels_.push_back(wake_level(wake_covered_until(wake_second_end_), wake_second_end_ - wake_from_));
        ++wake_second_;
        wake_from_ = wake_second_end_;
        wake_second_end_ = signal_map_second_start(wake_second_ + 1);
        wake_covered_ = 0;
    }

    // Publish the wake: every second completed since the recording began, and
    // the passed part of the one under the head.
    void publish_wake() {
        wake_publish_buffer_ = wake_levels_;
        if (position_ > wake_from_) {
            wake_publish_buffer_.push_back(wake_level(wake_covered_until(position_), position_ - wake_from_));
        }
        signal_map_.update(wake_first_second_, wake_publish_buffer_);
        wake_next_publish_ = position_ + SIGNAL_MAP_RECORDING_PUBLISH_CLOCKS;
    }

    // Write `bytes`, the tape's encoded image, to its file, and mark it clean.
    bool write_back(const std::vector<uint8_t>& bytes) {
        if (!write_atomically(bytes)) return false;
        tape_->recorder()->mark_clean();
        return true;
    }

    // Map a tape just inserted: from its file, or a UEF's image with none.
    void map_inserted_tape() {
        if (live_) {
            signal_map_.clear();
        } else if (!filepath_.empty()) {
            std::optional<WavChannel> channel;
            if (tape_->metadata().channel == "left") channel = WavChannel::Left;
            if (tape_->metadata().channel == "right") channel = WavChannel::Right;
            signal_map_.map([filepath = filepath_, channel] { return open_tape_file(filepath, channel); });
        } else if (TapeRecorder* recorder = tape_->recorder()) {
            map_image(std::make_shared<const std::vector<uint8_t>>(recorder->encode()));
        } else {
            signal_map_.clear();
        }
    }

    void map_image(std::shared_ptr<const std::vector<uint8_t>> bytes, bool keep_current = false) {
        signal_map_.map([bytes = std::move(bytes), name = name_] { return open_tape_image(*bytes, name); },
                        keep_current);
    }

    // Whether this process may write `path`, a file or a folder.
    static bool can_write(const std::filesystem::path& path) {
#ifdef _WIN32
        return _waccess(path.c_str(), 2) == 0;
#else
        return ::access(path.c_str(), W_OK) == 0;
#endif
    }

    bool write_atomically(const std::vector<uint8_t>& bytes) const {
        std::filesystem::path temporary = filepath_;
        temporary += ".beebium-writing";
        std::error_code ec;
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            if (out) {
                out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                out.close();
            }
            if (!out) {
                std::filesystem::remove(temporary, ec);
                std::cerr << "The tape " << name_ << " could not be written back to " << filepath_.string()
                          << ", so its recording is kept only in memory.\n";
                return false;
            }
        }
        std::filesystem::rename(temporary, filepath_, ec);
        if (ec) {
            const std::string reason = ec.message();
            std::filesystem::remove(temporary, ec);
            std::cerr << "The tape " << name_ << " could not be written back to " << filepath_.string() << " ("
                      << reason << "), so its recording is kept only in memory.\n";
            return false;
        }
        return true;
    }

    bool at_end() const {
        return pending_.type == TapeItem::Type::End && position_ >= pending_.time;
    }

    // The tape's length, or where the reader is if the tape does not know it.
    TapeTime end_time() const {
        return tape_->duration().value_or(position_);
    }

    // One ULA clock of winding. The reader is left where it was and caught
    // up by end_wind().
    void wind() {
        if (transport_ == Transport::Rewinding) {
            position_ = position_ > WIND_SPEED ? position_ - WIND_SPEED : 0;
            if (position_ == 0) finish_wind();
        } else {
            const TapeTime end = end_time();
            position_ = end - position_ > WIND_SPEED ? position_ + WIND_SPEED : end;
            if (position_ == end) finish_wind();
        }
        if (transport_ != Transport::Stopped && ++clocks_since_publish_ >= POSITION_PUBLISH_CLOCKS) {
            clocks_since_publish_ = 0;
            published_position_.store(position_, std::memory_order_relaxed);
        }
    }

    // A wind reaching an end of the tape: the deck stops there.
    void finish_wind() {
        end_wind();
        transport_ = Transport::Stopped;
        publish();
    }

    // Bring the reader to where winding left the tape.
    void end_wind() {
        if (!winding()) return;
        tape_->seek(position_);
        pending_ = tape_->next();
    }

    bool advance() {
        bool edge = false;
        // Take every item due by now: an edge on this clock is reported; an
        // edge already passed (after a seek) and an ended gap are dropped.
        while (pending_.type != TapeItem::Type::End && pending_.time <= position_) {
            if (pending_.type == TapeItem::Type::Edge && pending_.time == position_) edge = true;
            pending_ = tape_->next();
        }
        if (pending_.type == TapeItem::Type::End && position_ >= pending_.time) {
            transport_ = Transport::Stopped;
            publish();
            return edge;
        }
        ++position_;
        if (++clocks_since_publish_ >= POSITION_PUBLISH_CLOCKS) {
            clocks_since_publish_ = 0;
            published_position_.store(position_, std::memory_order_relaxed);
        }
        return edge;
    }

    // Publish everything but the relay (which clock() publishes as it changes).
    void publish() {
        clocks_since_publish_ = 0;
        published_has_tape_.store(tape_ != nullptr, std::memory_order_relaxed);
        published_transport_.store(transport_, std::memory_order_relaxed);
        published_position_.store(position_, std::memory_order_relaxed);
        published_sequence_.fetch_add(1, std::memory_order_release);
    }

    std::unique_ptr<TapeSignal> tape_;
    std::string name_;
    std::string url_;
    std::filesystem::path filepath_;
    bool configured_ = false;
    bool live_ = false;           // the tape is a live input
    bool head_writing_ = false;   // the head writes on this clock
    TapeTime head_time_ = 0;      // the tape time under the head on this clock
    Transport transport_ = Transport::Stopped;
    TapeTime position_ = 0;
    TapeItem pending_ = TapeItem::end(0);
    bool relay_closed_ = false;
    uint32_t clocks_since_publish_ = 0;

    // The signal map in the head's wake while recording (emulation thread only).
    uint64_t wake_first_second_ = 0;     // the second the recording began in
    uint64_t wake_second_ = 0;           // the second under the head
    TapeTime wake_second_end_ = 0;       // where it ends
    TapeTime wake_from_ = 0;             // where its passed part begins
    TapeTime wake_covered_ = 0;          // clocks of that part in a run, to the last edge
    bool wake_in_run_ = false;
    TapeTime wake_last_edge_ = 0;
    TapeTime wake_next_publish_ = 0;
    std::vector<uint8_t> wake_levels_;   // completed seconds since wake_first_second_
    std::vector<uint8_t> wake_publish_buffer_;

    std::atomic<bool> published_has_tape_{false};
    std::atomic<Transport> published_transport_{Transport::Stopped};
    std::atomic<TapeTime> published_position_{0};
    std::atomic<bool> published_relay_{false};
    std::atomic<uint64_t> published_tape_generation_{0};
    std::atomic<uint64_t> published_sequence_{0};

    // Last, so it is destroyed first: its destructor cancels the walk and
    // joins the worker before anything else of the deck goes.
    SignalMapper signal_map_;
};

// A hardware policy with a cassette interface declares `has_cassette = true`
// and owns a `cassette_deck`.
template <typename T>
concept HasCassette = requires(T& hardware) {
    requires T::has_cassette;
    { hardware.cassette_deck } -> std::convertible_to<CassetteDeck&>;
};

}  // namespace beebium
