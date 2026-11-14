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

#ifndef BEEBIUM_EXTENSION_CONTROLLER_HPP
#define BEEBIUM_EXTENSION_CONTROLLER_HPP

#include "Export.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace beebium {

// What a control is: an axis, 0 to 65535 with the centre at 32768, or a
// button, pressed or not.
enum class ControlKind { Axis, Button };

// One control a controller publishes.
struct ControlDescription {
    std::string id;            // stable 7-bit ASCII, fixed by the device: "stick1.x", "fire1"
    std::string display_name;  // "Stick 1 X", "Fire 1"
    std::string group;         // "Stick 1", "Handset 2"; empty for a flat device
    ControlKind kind;
};

// A controller is a device a person holds or points at the screen: a
// joystick, a handset, a mouse, a light pen. It publishes named controls,
// each an axis or a button, in the device's own vocabulary, and a client
// sets them through ControllerService as it presses BBC keys through
// KeyboardService. The server does no interpretation: dead zone, inversion
// and calibration are the client's.
//
// The set_* and getters are called on gRPC threads. A controller stores its
// controls in atomics, so the emulation thread reads them from its port seam
// (AnaloguePortDevice, UserPortDevice) with plain loads. A control is a
// physical thing someone is holding: its value persists across Break and
// power-on.
struct BEEBIUM_EXT_API Controller {
    virtual ~Controller() = default;

    // The extension instance id, as PeripheralExtensionService names it.
    virtual std::string_view controller_id() const = 0;
    // What a user calls it: "Joystick".
    virtual std::string_view display_name() const = 0;
    // Every control, in the device's order.
    virtual std::span<const ControlDescription> controls() const = 0;

    // The value of an axis, 0 to 65535, or whether a button is pressed. Throw
    // std::invalid_argument for an id the device has no control of that kind
    // named.
    virtual uint16_t axis(std::string_view id) const = 0;
    virtual bool button(std::string_view id) const = 0;

    // Set an axis or a button. Throw std::invalid_argument, with a sentence
    // naming the id, for an id the device has no control of that kind named.
    virtual void set_axis(std::string_view id, uint16_t value) = 0;
    virtual void set_button(std::string_view id, bool pressed) = 0;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_CONTROLLER_HPP
