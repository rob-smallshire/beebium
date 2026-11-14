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

#include "JoystickExtension.hpp"

#include <beebium/extension/AnaloguePort.hpp>
#include <beebium/extension/ControllerRegistry.hpp>
#include <beebium/extension/ExtensionContext.hpp>

#include <stdexcept>
#include <string>

namespace beebium {

namespace {

constexpr uint16_t CENTRE = 32768;

const ControlDescription CONTROLS[] = {
    {"stick1.x", "Stick 1 X", "Stick 1", ControlKind::Axis},
    {"stick1.y", "Stick 1 Y", "Stick 1", ControlKind::Axis},
    {"fire1", "Fire 1", "Stick 1", ControlKind::Button},
    {"stick2.x", "Stick 2 X", "Stick 2", ControlKind::Axis},
    {"stick2.y", "Stick 2 Y", "Stick 2", ControlKind::Axis},
    {"fire2", "Fire 2", "Stick 2", ControlKind::Button},
};

// The channel an axis drives, or -1.
int axis_channel(std::string_view control_id) {
    if (control_id == "stick1.x") return 0;
    if (control_id == "stick1.y") return 1;
    if (control_id == "stick2.x") return 2;
    if (control_id == "stick2.y") return 3;
    return -1;
}

// The button line a button grounds, in place on System VIA port B, or 0.
uint8_t button_line(std::string_view control_id) {
    if (control_id == "fire1") return 0x10;
    if (control_id == "fire2") return 0x20;
    return 0;
}

[[noreturn]] void no_such_control(std::string_view control_id, std::string_view kind) {
    throw std::invalid_argument("The joystick has no " + std::string(kind) + " named " +
                                std::string(control_id) + ".");
}

}  // namespace

JoystickExtension::JoystickExtension() {
    for (auto& axis : axes_) axis.store(CENTRE, std::memory_order_relaxed);
}

JoystickExtension::~JoystickExtension() = default;

std::span<const std::string_view> JoystickExtension::attaches_to() const {
    static constexpr std::string_view points[] = {"analogue-port"};
    return points;
}

std::span<const std::string_view> JoystickExtension::provides() const {
    return {};
}

void JoystickExtension::init(ExtensionContext& ctx) {
    ctx.get<AnaloguePort>().attach(*this);
    if (ctx.has<ControllerRegistry>()) {
        registry_ = &ctx.get<ControllerRegistry>();
        registry_->add(*this);
    }
}

void JoystickExtension::shutdown() {
    if (registry_) {
        registry_->remove(*this);
        registry_ = nullptr;
    }
}

std::span<const ControlDescription> JoystickExtension::controls() const {
    return CONTROLS;
}

uint16_t JoystickExtension::axis(std::string_view control_id) const {
    const int channel = axis_channel(control_id);
    if (channel < 0) no_such_control(control_id, "axis");
    return axes_[static_cast<size_t>(channel)].load(std::memory_order_relaxed);
}

bool JoystickExtension::button(std::string_view control_id) const {
    const uint8_t line = button_line(control_id);
    if (line == 0) no_such_control(control_id, "button");
    return (pressed_.load(std::memory_order_relaxed) & line) != 0;
}

void JoystickExtension::set_axis(std::string_view control_id, uint16_t value) {
    const int channel = axis_channel(control_id);
    if (channel < 0) no_such_control(control_id, "axis");
    axes_[static_cast<size_t>(channel)].store(value, std::memory_order_relaxed);
}

void JoystickExtension::set_button(std::string_view control_id, bool pressed) {
    const uint8_t line = button_line(control_id);
    if (line == 0) no_such_control(control_id, "button");
    if (pressed) {
        pressed_.fetch_or(line, std::memory_order_relaxed);
    } else {
        pressed_.fetch_and(static_cast<uint8_t>(~line), std::memory_order_relaxed);
    }
}

}  // namespace beebium
