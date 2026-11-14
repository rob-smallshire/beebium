// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_EXT_VOLTMACE_DELTA_14B_EXTENSION_HPP
#define BEEBIUM_EXT_VOLTMACE_DELTA_14B_EXTENSION_HPP

#include <beebium/extension/AnaloguePortDevice.hpp>
#include <beebium/extension/Controller.hpp>
#include <beebium/extension/PeripheralExtension.hpp>
#include <beebium/extension/UserPortDevice.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <span>
#include <string_view>

namespace beebium {

class ControllerRegistry;

// Built-in PeripheralExtension: Voltmace's Delta 14B/1 adapter box with two
// Delta 14B handsets, in the analogue port and the user port together. It is
// the analogue port's device, the user port's device and one controller.
//
// Handset 1 is the rear socket, on channels 0 (X) and 1 (Y); handset 2 the
// side socket, on channels 2 and 3. Each has a 3 by 4 keypad, keys 0 to 11,
// key = 3 * row + column, row 0 the bottom row. The two red buttons are wired
// to key 10. Through the user port:
//
//   PB4, PB5, PB6  column 0, 1, 2 strobes, common to both handsets, active low
//   PB3 .. PB0     row 0 .. 3 of the selected handset, active low, 1 at rest
//   PB7            74LS157 select: 0 = rear (handset 1), 1 = side (handset 2)
//
// The rows are computed on every update_port_b() from the keys held and the
// lines the user VIA drives at that instant, never stored, so a strobe
// written to &FE60 is visible on the very next read of it. A column is
// driven low only when its DDRB bit is an output and its ORB bit is 0.
// Ghosting through the diode-less matrix is not modelled.
//
// Three behaviours are unverified on a real box (docs/analogue-port.md):
// PB7 as an input selects the side handset; a row 3 key on handset 1
// grounds analogue connector PB0 (System VIA PB4) and one on handset 2
// connector PB1 (PB5), each only while its column is driven low; and the
// user port's CB1 and CB2 are left alone.
//
// CLI: --voltmace-delta-14b (no parameters).
class VoltmaceDelta14bExtension : public PeripheralExtension,
                                  public AnaloguePortDevice,
                                  public UserPortDevice,
                                  public Controller {
public:
    VoltmaceDelta14bExtension();
    ~VoltmaceDelta14bExtension() override;

    // PeripheralExtension
    std::span<const std::string_view> attaches_to() const override;
    std::span<const std::string_view> provides() const override;
    void init(ExtensionContext& ctx) override;
    void shutdown() override;

    // AnaloguePortDevice: on the emulation thread.
    uint16_t channel_level(int channel) override {
        return axes_[static_cast<size_t>(channel & 3)].load(std::memory_order_relaxed);
    }
    uint8_t button_lines() override;

    // UserPortDevice: on the emulation thread. CB1 and CB2 are left alone.
    uint8_t update_port_b(uint8_t output, uint8_t ddr) override;

    // Controller
    std::string_view controller_id() const override { return id(); }
    std::string_view display_name() const override { return "Voltmace Delta 14B/1 adapter"; }
    std::span<const ControlDescription> controls() const override;
    uint16_t axis(std::string_view control_id) const override;
    bool button(std::string_view control_id) const override;
    void set_axis(std::string_view control_id, uint16_t value) override;
    void set_button(std::string_view control_id, bool pressed) override;

private:
    // Indexed by channel: handset1.x, handset1.y, handset2.x, handset2.y.
    std::array<std::atomic<uint16_t>, 4> axes_;
    // Per handset, bit k set while key k is held.
    std::array<std::atomic<uint16_t>, 2> keys_;
    // The user VIA's port B output and DDR as it last presented them, for
    // the fire lines. Written and read on the emulation thread.
    std::atomic<uint8_t> user_output_{0};
    std::atomic<uint8_t> user_ddr_{0};
    ControllerRegistry* registry_ = nullptr;
};

}  // namespace beebium

#endif  // BEEBIUM_EXT_VOLTMACE_DELTA_14B_EXTENSION_HPP
