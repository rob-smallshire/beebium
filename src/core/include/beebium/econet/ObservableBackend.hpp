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

#ifndef BEEBIUM_ECONET_OBSERVABLE_BACKEND_HPP
#define BEEBIUM_ECONET_OBSERVABLE_BACKEND_HPP

#include "NetworkBackend.hpp"

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace beebium {

// A NetworkBackend decorator that records the frames crossing it.
//
// Sits directly above the wire-side backend, so what it records is what
// actually left or arrived at the transport -- typed AUN frames, after the
// four-way handshake has done its translation. That is the view worth having
// when a conversation with a peer goes wrong: it answers "what did we actually
// put on the network, and what came back", which the guest's screen cannot.
//
// Threading. send_frame/receive_frame run on the emulation thread; readers are
// gRPC handler threads. The ring is guarded by a mutex held only long enough
// to copy one fixed-size record, and the emulation thread never allocates
// here: payloads are truncated into an inline array rather than copied into a
// vector. Readers poll by sequence number rather than being pushed to, so a
// slow or vanished subscriber cannot hold up emulation -- it simply misses
// events, which the sequence gap makes visible.
class ObservableBackend : public NetworkBackend {
public:
    // How much of a frame's payload is retained. Enough to identify a
    // fileserver command or reply without turning the ring into a packet
    // capture; `data_length` always reports the true size.
    static constexpr std::size_t MAX_PAYLOAD = 64;

    // Events retained. Old events are overwritten once full: a subscriber that
    // falls this far behind has lost the thread anyway, and dropping the
    // oldest keeps the memory bounded.
    static constexpr std::size_t CAPACITY = 256;

    enum class EventType : std::uint8_t {
        FrameSent,
        FrameReceived,
        ConnectionChanged,
    };

    struct Event {
        std::uint64_t sequence = 0;
        EventType type = EventType::FrameSent;

        // Frame detail. Meaningful for FrameSent and FrameReceived.
        FrameType frame_type = FrameType::RawFrame;
        std::uint8_t port = 0;
        std::uint8_t control_byte = 0;
        std::uint8_t dest_net = 0;
        std::uint8_t dest_stn = 0;
        std::uint8_t src_net = 0;
        std::uint8_t src_stn = 0;
        std::uint32_t data_length = 0;   // true length, before truncation
        std::uint8_t data[MAX_PAYLOAD] = {};
        std::uint8_t data_captured = 0;  // bytes actually retained

        // Meaningful for ConnectionChanged.
        bool connected = false;
    };

    explicit ObservableBackend(NetworkBackend& wrapped) : wrapped_(wrapped) {
        last_connected_ = wrapped_.is_connected();
    }

    // --- NetworkBackend interface ---

    void send_frame(const NetworkFrame& frame) override {
        record_frame(EventType::FrameSent, frame);
        wrapped_.send_frame(frame);
    }

    std::optional<NetworkFrame> receive_frame() override {
        auto frame = wrapped_.receive_frame();
        if (frame) record_frame(EventType::FrameReceived, *frame);
        return frame;
    }

    bool is_connected() const override {
        bool connected = wrapped_.is_connected();
        // Recorded from the polling path rather than pushed by the backend,
        // because a backend's connection state can change on its own thread
        // (a cable unplug, a USB hot-remove) and this is the point at which
        // the emulator notices.
        if (connected != last_connected_) {
            last_connected_ = connected;
            record_connection(connected);
        }
        return connected;
    }

    bool is_receiving_flags() const override {
        return wrapped_.is_receiving_flags();
    }

    bool is_expecting_frame() const override {
        return wrapped_.is_expecting_frame();
    }

    bool is_reachable(std::uint8_t net, std::uint8_t stn) const override {
        return wrapped_.is_reachable(net, stn);
    }

    void on_station_id_changed(std::uint8_t new_station_id) override {
        wrapped_.on_station_id_changed(new_station_id);
    }

    // --- Reader interface ---

    // The sequence number the next event will carry. A subscriber starting
    // now passes this to collect_since() to receive only what follows.
    std::uint64_t next_sequence() const {
        std::lock_guard lock(mutex_);
        return next_sequence_;
    }

    // Events with a sequence >= `from`, oldest first, up to `limit`.
    //
    // Events older than the ring's reach are simply absent; the caller can
    // detect that by comparing the first returned sequence with the one it
    // asked for. Advancing `from` past the gap is the caller's decision, not
    // ours -- silently renumbering would hide the loss.
    std::vector<Event> collect_since(std::uint64_t from,
                                     std::size_t limit = CAPACITY) const {
        std::vector<Event> out;
        std::lock_guard lock(mutex_);
        if (count_ == 0 || limit == 0) return out;

        const std::uint64_t oldest = next_sequence_ - count_;
        std::uint64_t start = from > oldest ? from : oldest;
        if (start >= next_sequence_) return out;

        out.reserve(static_cast<std::size_t>(
            std::min<std::uint64_t>(next_sequence_ - start, limit)));
        for (std::uint64_t seq = start; seq < next_sequence_ && out.size() < limit;
             ++seq) {
            out.push_back(ring_[static_cast<std::size_t>(seq % CAPACITY)]);
        }
        return out;
    }

    // Events discarded because a subscriber fell further behind than the ring
    // reaches. Non-zero means the event stream has holes in it.
    std::uint64_t overwritten_count() const {
        std::lock_guard lock(mutex_);
        return next_sequence_ >= CAPACITY ? next_sequence_ - CAPACITY
                                          : 0;
    }

private:
    void record_frame(EventType type, const NetworkFrame& frame) const {
        Event event;
        event.type = type;
        event.frame_type = frame.type;
        event.port = frame.port;
        event.control_byte = frame.control_byte;
        event.dest_net = frame.dest_net;
        event.dest_stn = frame.dest_stn;
        event.src_net = frame.src_net;
        event.src_stn = frame.src_stn;
        event.data_length = static_cast<std::uint32_t>(frame.data.size());
        const std::size_t captured = std::min(frame.data.size(), MAX_PAYLOAD);
        for (std::size_t i = 0; i < captured; ++i) event.data[i] = frame.data[i];
        event.data_captured = static_cast<std::uint8_t>(captured);
        push(event);
    }

    void record_connection(bool connected) const {
        Event event;
        event.type = EventType::ConnectionChanged;
        event.connected = connected;
        push(event);
    }

    void push(Event& event) const {
        std::lock_guard lock(mutex_);
        event.sequence = next_sequence_;
        ring_[static_cast<std::size_t>(next_sequence_ % CAPACITY)] = event;
        ++next_sequence_;
        if (count_ < CAPACITY) ++count_;
    }

    NetworkBackend& wrapped_;

    mutable std::mutex mutex_;
    mutable std::array<Event, CAPACITY> ring_{};
    mutable std::uint64_t next_sequence_ = 0;
    mutable std::size_t count_ = 0;
    mutable bool last_connected_ = false;
};

}  // namespace beebium

#endif  // BEEBIUM_ECONET_OBSERVABLE_BACKEND_HPP
