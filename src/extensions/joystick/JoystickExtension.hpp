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

#ifndef BEEBIUM_EXT_JOYSTICK_EXTENSION_HPP
#define BEEBIUM_EXT_JOYSTICK_EXTENSION_HPP

#include <beebium/extension/AnaloguePortDevice.hpp>
#include <beebium/extension/Controller.hpp>
#include <beebium/extension/PeripheralExtension.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <span>
#include <string_view>

namespace beebium {

class ControllerRegistry;

// Built-in PeripheralExtension: two analogue joysticks with a fire button
// each, plugged into the analogue port. It is the port's device and a
// controller.
//
// Controls, in this order: stick1.x (channel 0), stick1.y (channel 1),
// fire1 (grounds PB4, connector PB0), stick2.x (channel 2), stick2.y
// (channel 3), fire2 (grounds PB5, connector PB1). Axes are the channel
// levels themselves, 0 to 65535, and start at the centre, 32768; buttons
// start released. Nothing resets them: they are where the person holding
// the sticks left them. A Voltmace Delta handset plugged straight into the
// port is electrically this device.
//
// CLI: --joystick (no parameters).
class JoystickExtension : public PeripheralExtension,
                          public AnaloguePortDevice,
                          public Controller {
public:
    JoystickExtension();
    ~JoystickExtension() override;

    // PeripheralExtension
    std::span<const std::string_view> attaches_to() const override;
    std::span<const std::string_view> provides() const override;
    void init(ExtensionContext& ctx) override;
    void shutdown() override;

    // AnaloguePortDevice: plain loads, on the emulation thread.
    uint16_t channel_level(int channel) override {
        return axes_[static_cast<size_t>(channel & 3)].load(std::memory_order_relaxed);
    }
    uint8_t button_lines() override {
        return static_cast<uint8_t>(0x30 & ~pressed_.load(std::memory_order_relaxed));
    }

    // Controller
    std::string_view controller_id() const override { return id(); }
    std::string_view display_name() const override { return "Joystick"; }
    std::span<const ControlDescription> controls() const override;
    uint16_t axis(std::string_view control_id) const override;
    bool button(std::string_view control_id) const override;
    void set_axis(std::string_view control_id, uint16_t value) override;
    void set_button(std::string_view control_id, bool pressed) override;

private:
    // Indexed by channel: stick1.x, stick1.y, stick2.x, stick2.y.
    std::array<std::atomic<uint16_t>, 4> axes_;
    // The button lines pressed, in place as the System VIA reads them: bit 4
    // for fire1, bit 5 for fire2.
    std::atomic<uint8_t> pressed_{0};
    ControllerRegistry* registry_ = nullptr;
};

}  // namespace beebium

#endif  // BEEBIUM_EXT_JOYSTICK_EXTENSION_HPP
