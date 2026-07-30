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

#include <atomic>
#include <cstdint>
#include <optional>
#include <vector>

namespace beebium {

// AUN packet types for typed frame transport between the ADLC and network backend.
// Values match the AUN wire format.
enum class FrameType : uint8_t {
    RawFrame   = 0,  // Raw Econet frame (non-AUN passthrough)
    Broadcast  = 1,  // AUN broadcast (fire-and-forget)
    Unicast    = 2,  // AUN unicast data transfer
    Ack        = 3,  // AUN acknowledgement
    Nack       = 4,  // AUN negative acknowledgement
    Immediate  = 5,  // AUN immediate operation request
    ImmReply   = 6,  // AUN immediate operation reply
};

// A typed network frame carrying Econet/AUN addressing and payload.
//
// For RawFrame: data contains the complete Econet frame bytes (address + control + data).
// For typed AUN frames: addressing is in the header fields, data contains the payload
// after the control byte and port.
struct NetworkFrame {
    FrameType type = FrameType::RawFrame;
    uint8_t port = 0;
    uint8_t control_byte = 0;
    uint8_t dest_net = 0;
    uint8_t dest_stn = 0;
    uint8_t src_net = 0;
    uint8_t src_stn = 0;
    std::vector<uint8_t> data;
};

// Abstract network transport for the MC6854 ADLC.
//
// Decouples the ADLC from the underlying network transport (UDP/AUN, loopback, test
// double). The AunBackend currently uses non-blocking socket I/O with select() at
// zero timeout. This introduces syscalls on the emulation thread — best-case ~1-2us,
// but potentially much worse under system load (context switches, scheduling delays).
// At 2MHz (0.5us/cycle) even the best case is several cycles of wall-clock time.
// This is tolerable for now because receive_frame() is only called when the ADLC's
// RX frame buffer is drained (roughly once per inter-frame gap, not every cycle).
// A future improvement would be a separate I/O thread with lock-free queues to
// eliminate all syscalls from the emulation path.
class NetworkBackend {
public:
    virtual ~NetworkBackend() = default;

    // Send a complete frame with type metadata.
    // For RawFrame, data contains the full Econet frame bytes.
    // For AUN types, header fields carry addressing and data carries the payload.
    virtual void send_frame(const NetworkFrame& frame) = 0;

    // Non-blocking receive. Returns the next complete frame if one is available,
    // or std::nullopt if the receive queue is empty.
    virtual std::optional<NetworkFrame> receive_frame() = 0;

    // Whether the network link is active (DCD sense: true = carrier present).
    virtual bool is_connected() const = 0;

    // Whether the network is currently receiving flag sequences (inter-frame fill).
    // The ADLC uses this to drive Flag Detected (SR1b3).
    // On real hardware this comes from the physical line; in emulation, the
    // four-way handshake bridge generates synthetic flag fill during transactions.
    virtual bool is_receiving_flags() const { return false; }

    // Whether another frame is expected imminently (inter-frame gap in a
    // multi-frame handshake). On real Econet, flag fill continues between
    // scout and data frames, so the MC6854 does not report INACTIVE during
    // these gaps. The ADLC uses this to suppress Rx Idle (SR2 INACTIVE)
    // during inter-frame handshake gaps.
    virtual bool is_expecting_frame() const { return false; }

    // Whether a frame addressed to (net, stn) could be delivered at all.
    //
    // Asked before the four-way handshake commits to a transaction, because
    // its scout acknowledgement is synthesised before anything leaves the
    // machine: without this the guest is told a station that does not exist
    // answered it. A backend that can know in advance -- AunBackend has a peer
    // table -- answers honestly, and the handshake then lets the line fall
    // idle so the guest reaches the conclusion it would reach on a real wire.
    //
    // Backends that cannot know until they have tried (Piconet, whose
    // firmware performs the wire handshake) leave this true and report failure
    // afterwards instead, by delivering a Nack. Answering optimistically is
    // the safe default: a false negative refuses traffic that would have been
    // delivered.
    virtual bool is_reachable(uint8_t /*net*/, uint8_t /*stn*/) const {
        return true;
    }

    // Notify the backend that EconetSocket's station ID has changed. The
    // initial station ID is communicated via the backend's constructor or
    // configuration, NOT via this hook -- enable() does not call it. Backends
    // for which station identity is invisible (AunBackend, TestBackend)
    // ignore this. Backends with their own station-bearing apparatus
    // (PiconetBackend, where the Piconet device must be told via SET_STATION)
    // override to propagate.
    virtual void on_station_id_changed(uint8_t /*new_station_id*/) {}

    // Monotonic counter bumped by the backend whenever status visible on
    // EconetStatus changes (connection toggle, port change, etc.). Read by
    // WatchEconetStatus's poll loop to detect transport-level state changes
    // that bypass EconetSocket (e.g. AunService.SetConnected mutating
    // AunBackend directly). Backends that have no such state leave this at 0.
    uint64_t backend_status_sequence() const {
        return backend_status_sequence_.load(std::memory_order_acquire);
    }

protected:
    void bump_backend_status_sequence() {
        backend_status_sequence_.fetch_add(1, std::memory_order_acq_rel);
    }

private:
    std::atomic<uint64_t> backend_status_sequence_{0};
};

}  // namespace beebium
