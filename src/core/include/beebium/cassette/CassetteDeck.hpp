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
#include <concepts>
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
// The Serial ULA clocks the deck on the emulation thread and takes the edges
// that fall on each clock. CassetteService drives the transport with the
// emulation paused, and reads the deck's state without pausing it through
// the atomics the deck publishes: presence, transport, relay and a tape
// generation when they change, and the position whenever the transport moves
// and every POSITION_PUBLISH_CLOCKS while playing, so the clock path pays a
// counter rather than a store a clock.
class CassetteDeck {
public:
    enum class Transport : uint8_t { Stopped, Playing, Rewinding, FastForwarding };

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

    // Throws std::runtime_error with a sentence if a tape is already in.
    // `name` is what the tape is called in messages; `url` is its source as
    // given, for reporting.
    void insert(std::unique_ptr<TapeSignal> tape, std::string name, std::string url = {}) {
        if (tape_) {
            throw std::runtime_error("The deck already holds " + name_ +
                                     "; eject it before inserting another tape.");
        }
        tape_ = std::move(tape);
        name_ = std::move(name);
        url_ = std::move(url);
        configured_ = false;
        transport_ = Transport::Stopped;
        position_ = 0;
        tape_->rewind();
        pending_ = tape_->next();
        published_tape_generation_.fetch_add(1, std::memory_order_relaxed);
        publish();
    }

    void eject() {
        tape_.reset();
        name_.clear();
        url_.clear();
        configured_ = false;
        transport_ = Transport::Stopped;
        position_ = 0;
        pending_ = TapeItem::end(0);
        published_tape_generation_.fetch_add(1, std::memory_order_relaxed);
        publish();
    }

    void play() {
        end_wind();
        if (tape_ && !at_end()) transport_ = Transport::Playing;
        publish();
    }

    void stop() {
        end_wind();
        transport_ = Transport::Stopped;
        publish();
    }

    // Wind backwards to the start, where the deck stops.
    void rewind() {
        if (!tape_) return;
        end_wind();
        transport_ = position_ > 0 ? Transport::Rewinding : Transport::Stopped;
        publish();
    }

    // Wind forwards to the end, where the deck stops.
    void fast_forward() {
        if (!tape_) return;
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
        if (winding()) wind();
        return false;
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
    bool configured_ = false;
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
