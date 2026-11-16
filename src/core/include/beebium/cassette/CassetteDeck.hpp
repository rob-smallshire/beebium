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
// The Serial ULA clocks the deck on the emulation thread and takes the edges
// that fall on each clock; the service will drive the transport.
class CassetteDeck {
public:
    enum class Transport : uint8_t { Stopped, Playing };

    // Throws std::runtime_error with a sentence if a tape is already in.
    void insert(std::unique_ptr<TapeSignal> tape, std::string name) {
        if (tape_) {
            throw std::runtime_error("The deck already holds " + name_ +
                                     "; eject it before inserting another tape.");
        }
        tape_ = std::move(tape);
        name_ = std::move(name);
        transport_ = Transport::Stopped;
        position_ = 0;
        tape_->rewind();
        pending_ = tape_->next();
    }

    void eject() {
        tape_.reset();
        name_.clear();
        transport_ = Transport::Stopped;
        position_ = 0;
        pending_ = TapeItem::end(0);
    }

    void play() {
        if (tape_ && !at_end()) transport_ = Transport::Playing;
    }

    void stop() { transport_ = Transport::Stopped; }

    // Reposition instantly; a time beyond the end lands at the end and stops.
    void seek(TapeTime time) {
        if (!tape_) return;
        const auto length = tape_->duration();
        if (length && time >= *length) {
            time = *length;
            transport_ = Transport::Stopped;
        }
        tape_->seek(time);
        position_ = time;
        pending_ = tape_->next();
    }

    void rewind() { seek(0); }

    // One ULA clock with the relay as the machine has it. Returns true if an
    // edge of the tape falls on this clock.
    bool clock(bool relay_closed) {
        if (transport_ != Transport::Playing || !relay_closed) {
            return false;
        }
        return advance();
    }

    TapeTime position() const { return position_; }
    std::optional<TapeTime> duration() const {
        return tape_ ? tape_->duration() : std::nullopt;
    }
    Transport transport() const { return transport_; }
    bool has_tape() const { return tape_ != nullptr; }
    const std::string& name() const { return name_; }
    const TapeMetadata& metadata() const {
        static const TapeMetadata none;
        return tape_ ? tape_->metadata() : none;
    }
    // Whether the tape is silent at the current position.
    bool in_gap() const { return pending_.type == TapeItem::Type::Gap; }

private:
    bool at_end() const {
        return pending_.type == TapeItem::Type::End && position_ >= pending_.time;
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
            return edge;
        }
        ++position_;
        return edge;
    }

    std::unique_ptr<TapeSignal> tape_;
    std::string name_;
    Transport transport_ = Transport::Stopped;
    TapeTime position_ = 0;
    TapeItem pending_ = TapeItem::end(0);
};

// A hardware policy with a cassette interface declares `has_cassette = true`
// and owns a `cassette_deck`.
template <typename T>
concept HasCassette = requires(T& hardware) {
    requires T::has_cassette;
    { hardware.cassette_deck } -> std::convertible_to<CassetteDeck&>;
};

}  // namespace beebium
