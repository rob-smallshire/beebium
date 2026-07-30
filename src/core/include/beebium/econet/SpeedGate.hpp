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

#ifndef BEEBIUM_ECONET_SPEED_GATE_HPP
#define BEEBIUM_ECONET_SPEED_GATE_HPP

#include "NetworkBackend.hpp"

#include <atomic>
#include <cstdint>
#include <optional>

namespace beebium {

// A NetworkBackend decorator that severs the Econet wire while the emulation is
// not running at real time.
//
// Inserted between the ADLC (or its FourWayHandshake) and the real backend for
// transports that require real-time pacing (Piconet bridging to a physical
// Econet line). While the referenced `gated` flag is set, outgoing frames are
// dropped, none are received, and the link reports disconnected with no flag
// fill -- so the guest's NFS sees a dead carrier and reports a network error,
// recovering naturally once the speed returns to 1x. When not gated it is a
// transparent pass-through. See docs/networking.md ("Emulation speed and
// real-time peers").
class SpeedGate : public NetworkBackend {
public:
    // `gated` is owned by the EconetSocket and outlives this decorator (both are
    // owned by the socket).
    SpeedGate(NetworkBackend& wrapped, const std::atomic<bool>& gated)
        : wrapped_(wrapped), gated_(gated) {}

    void send_frame(const NetworkFrame& frame) override {
        if (gated()) return;  // wire severed: drop outgoing traffic
        wrapped_.send_frame(frame);
    }

    std::optional<NetworkFrame> receive_frame() override {
        if (gated()) return std::nullopt;
        return wrapped_.receive_frame();
    }

    bool is_connected() const override {
        return !gated() && wrapped_.is_connected();
    }

    bool is_receiving_flags() const override {
        return !gated() && wrapped_.is_receiving_flags();
    }

    bool is_expecting_frame() const override {
        return !gated() && wrapped_.is_expecting_frame();
    }

    bool is_reachable(uint8_t net, uint8_t stn) const override {
        return !gated() && wrapped_.is_reachable(net, stn);
    }

    void on_station_id_changed(uint8_t new_station_id) override {
        // Station identity is configuration, not wire traffic -- always forward.
        wrapped_.on_station_id_changed(new_station_id);
    }

private:
    bool gated() const { return gated_.load(std::memory_order_relaxed); }

    NetworkBackend& wrapped_;
    const std::atomic<bool>& gated_;
};

}  // namespace beebium

#endif  // BEEBIUM_ECONET_SPEED_GATE_HPP
