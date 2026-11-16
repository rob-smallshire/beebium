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
#include "TapeSink.hpp"

#include <atomic>
#include <concepts>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

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
        name_ = std::move(name);
        url_ = std::move(url);
        filepath_ = std::move(filepath);
        configured_ = false;
        transport_ = Transport::Stopped;
        position_ = 0;
        tape_->rewind();
        pending_ = tape_->next();
        published_tape_generation_.fetch_add(1, std::memory_order_relaxed);
        publish();
    }

    void eject() {
        end_recording();
        flush();
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
        if (!tape_) return;
        end_recording();
        end_wind();
        transport_ = position_ > 0 ? Transport::Rewinding : Transport::Stopped;
        publish();
    }

    // Wind forwards to the end, where the deck stops.
    void fast_forward() {
        if (!tape_) return;
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
        if (!tape_) return;
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
        if (!tape_->recordable()) {
            throw std::runtime_error("The tape " + name_ + " is a " + kind_text() +
                                     " image, and Beebium records only on UEF tapes.");
        }
        if (transport_ == Transport::Recording) return;
        end_wind();
        if (position_ >= end_time()) {
            transport_ = Transport::Stopped;
        } else {
            transport_ = Transport::Recording;
            tape_->recorder()->begin_recording(position_);
        }
        publish();
    }

    bool recording() const { return transport_ == Transport::Recording; }

    // Whether the tape in the deck can be recorded on.
    bool recordable() const { return tape_ != nullptr && tape_->recorder() != nullptr; }

    // One ULA clock with the relay as the machine has it. Returns true if an
    // edge of the tape falls on this clock.
    bool clock(bool relay_closed) {
        if (relay_closed != relay_closed_) {
            relay_closed_ = relay_closed;
            published_relay_.store(relay_closed, std::memory_order_relaxed);
            published_position_.store(position_, std::memory_order_relaxed);
            published_sequence_.fetch_add(1, std::memory_order_release);
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
        if (head_writing_) tape_->recorder()->record_edge(head_time_, level);
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
        if (!write_atomically(bytes)) return false;
        recorder->mark_clean();
        return true;
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
    // Whether the tape is silent at the current position.
    bool in_gap() const { return pending_.type == TapeItem::Type::Gap; }

private:
    std::string kind_text() const {
        return tape_->kind() == TapeKind::Csw ? "CSW" : "UEF";
    }

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
        head_time_ = position_;
        head_writing_ = true;
        tape_->recorder()->erase_before(position_ + 1);
        ++position_;
        if (++clocks_since_publish_ >= POSITION_PUBLISH_CLOCKS) {
            clocks_since_publish_ = 0;
            published_position_.store(position_, std::memory_order_relaxed);
        }
    }

    // Leave the Recording transport, stopped, and write the tape back.
    void end_recording() {
        if (transport_ != Transport::Recording) return;
        head_writing_ = false;
        tape_->recorder()->end_recording();
        transport_ = Transport::Stopped;
        tape_->seek(position_);
        pending_ = tape_->next();
        flush();
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
    bool head_writing_ = false;   // the head writes on this clock
    TapeTime head_time_ = 0;      // the tape time under the head on this clock
    Transport transport_ = Transport::Stopped;
    TapeTime position_ = 0;
    TapeItem pending_ = TapeItem::end(0);
    bool relay_closed_ = false;
    uint32_t clocks_since_publish_ = 0;

    std::atomic<bool> published_has_tape_{false};
    std::atomic<Transport> published_transport_{Transport::Stopped};
    std::atomic<TapeTime> published_position_{0};
    std::atomic<bool> published_relay_{false};
    std::atomic<uint64_t> published_tape_generation_{0};
    std::atomic<uint64_t> published_sequence_{0};
};

// A hardware policy with a cassette interface declares `has_cassette = true`
// and owns a `cassette_deck`.
template <typename T>
concept HasCassette = requires(T& hardware) {
    requires T::has_cassette;
    { hardware.cassette_deck } -> std::convertible_to<CassetteDeck&>;
};

}  // namespace beebium
