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

#include "VoltmaceDelta14bExtension.hpp"

#include <beebium/extension/AnaloguePort.hpp>
#include <beebium/extension/ControllerRegistry.hpp>
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/UserPort.hpp>

#include <stdexcept>
#include <string>
#include <vector>

namespace beebium {

namespace {

constexpr uint16_t CENTRE = 32768;
constexpr int KEYS = 12;
constexpr uint16_t ROW_3_KEYS = (1u << 9) | (1u << 10) | (1u << 11);

std::vector<ControlDescription> make_controls() {
    std::vector<ControlDescription> controls;
    for (int handset = 1; handset <= 2; ++handset) {
        const std::string prefix = "handset" + std::to_string(handset) + ".";
        const std::string group = handset == 1 ? "Handset 1 (rear)" : "Handset 2 (side)";
        controls.push_back({prefix + "x", "X", group, ControlKind::Axis});
        controls.push_back({prefix + "y", "Y", group, ControlKind::Axis});
        for (int key = 0; key < KEYS; ++key) {
            controls.push_back({prefix + "key" + std::to_string(key),
                                "Key " + std::to_string(key), group, ControlKind::Button});
        }
    }
    return controls;
}

const std::vector<ControlDescription>& all_controls() {
    static const std::vector<ControlDescription> controls = make_controls();
    return controls;
}

// A control id parsed: handset 0 or 1, and either an axis (0 = x, 1 = y) or
// a key 0 to 11.
struct ControlRef {
    int handset = -1;
    int axis = -1;
    int key = -1;
};

ControlRef parse(std::string_view control_id) {
    ControlRef ref;
    if (control_id.starts_with("handset1.")) {
        ref.handset = 0;
    } else if (control_id.starts_with("handset2.")) {
        ref.handset = 1;
    } else {
        return {};
    }
    const std::string_view rest = control_id.substr(9);
    if (rest == "x") {
        ref.axis = 0;
    } else if (rest == "y") {
        ref.axis = 1;
    } else {
        for (int key = 0; key < KEYS; ++key) {
            if (rest == "key" + std::to_string(key)) {
                ref.key = key;
                break;
            }
        }
        if (ref.key < 0) return {};
    }
    return ref;
}

[[noreturn]] void no_such_control(std::string_view control_id, std::string_view kind) {
    throw std::invalid_argument("The Voltmace Delta 14B/1 adapter has no " + std::string(kind) +
                                " named " + std::string(control_id) + ".");
}

// The columns driven low, bit c for column c: those whose DDRB bit is an
// output and whose ORB bit is 0.
unsigned driven_low_columns(uint8_t output, uint8_t ddr) {
    return static_cast<unsigned>((ddr & ~output) >> 4) & 0x7u;
}

// The keys held in a driven-low column.
uint16_t keys_in_columns(uint16_t keys, unsigned columns) {
    uint16_t result = 0;
    for (int key = 0; key < KEYS; ++key) {
        if ((keys & (1u << key)) && (columns & (1u << (key % 3)))) {
            result = static_cast<uint16_t>(result | (1u << key));
        }
    }
    return result;
}

}  // namespace

VoltmaceDelta14bExtension::VoltmaceDelta14bExtension() {
    for (auto& axis : axes_) axis.store(CENTRE, std::memory_order_relaxed);
    for (auto& keys : keys_) keys.store(0, std::memory_order_relaxed);
}

VoltmaceDelta14bExtension::~VoltmaceDelta14bExtension() = default;

std::span<const std::string_view> VoltmaceDelta14bExtension::attaches_to() const {
    static constexpr std::string_view points[] = {"analogue-port", "user-port"};
    return points;
}

std::span<const std::string_view> VoltmaceDelta14bExtension::provides() const {
    return {};
}

void VoltmaceDelta14bExtension::init(ExtensionContext& ctx) {
    ctx.get<AnaloguePort>().attach(*this);
    ctx.get<UserPort>().attach(*this);
    if (ctx.has<ControllerRegistry>()) {
        registry_ = &ctx.get<ControllerRegistry>();
        registry_->add(*this);
    }
}

void VoltmaceDelta14bExtension::shutdown() {
    if (registry_) {
        registry_->remove(*this);
        registry_ = nullptr;
    }
}

uint8_t VoltmaceDelta14bExtension::update_port_b(uint8_t output, uint8_t ddr) {
    user_output_.store(output, std::memory_order_relaxed);
    user_ddr_.store(ddr, std::memory_order_relaxed);

    // Decision (unverified): an input select floats high and picks the side
    // handset.
    const bool side = !(ddr & 0x80) || (output & 0x80);
    const uint16_t held = keys_in_columns(
        keys_[side ? 1 : 0].load(std::memory_order_relaxed), driven_low_columns(output, ddr));

    // Row r is on PB(3 - r).
    uint8_t rows = 0x0F;
    for (int row = 0; row < 4; ++row) {
        if (held & (0x7u << (3 * row))) {
            rows = static_cast<uint8_t>(rows & ~(1u << (3 - row)));
        }
    }
    return static_cast<uint8_t>(0xF0 | rows);
}

uint8_t VoltmaceDelta14bExtension::button_lines() {
    // Decision (unverified): handset 1's row 3 keys reach connector PB0
    // (System VIA PB4) and handset 2's connector PB1 (PB5), each gated by its
    // column being driven low from the user port.
    const unsigned columns = driven_low_columns(user_output_.load(std::memory_order_relaxed),
                                                user_ddr_.load(std::memory_order_relaxed));
    uint8_t lines = 0x30;
    if (keys_in_columns(keys_[0].load(std::memory_order_relaxed), columns) & ROW_3_KEYS) {
        lines = static_cast<uint8_t>(lines & ~0x10);
    }
    if (keys_in_columns(keys_[1].load(std::memory_order_relaxed), columns) & ROW_3_KEYS) {
        lines = static_cast<uint8_t>(lines & ~0x20);
    }
    return lines;
}

std::span<const ControlDescription> VoltmaceDelta14bExtension::controls() const {
    return all_controls();
}

uint16_t VoltmaceDelta14bExtension::axis(std::string_view control_id) const {
    const auto ref = parse(control_id);
    if (ref.axis < 0) no_such_control(control_id, "axis");
    return axes_[static_cast<size_t>(2 * ref.handset + ref.axis)].load(std::memory_order_relaxed);
}

bool VoltmaceDelta14bExtension::button(std::string_view control_id) const {
    const auto ref = parse(control_id);
    if (ref.key < 0) no_such_control(control_id, "button");
    return (keys_[static_cast<size_t>(ref.handset)].load(std::memory_order_relaxed) &
            (1u << ref.key)) != 0;
}

void VoltmaceDelta14bExtension::set_axis(std::string_view control_id, uint16_t value) {
    const auto ref = parse(control_id);
    if (ref.axis < 0) no_such_control(control_id, "axis");
    axes_[static_cast<size_t>(2 * ref.handset + ref.axis)].store(value,
                                                                 std::memory_order_relaxed);
}

void VoltmaceDelta14bExtension::set_button(std::string_view control_id, bool pressed) {
    const auto ref = parse(control_id);
    if (ref.key < 0) no_such_control(control_id, "button");
    auto& keys = keys_[static_cast<size_t>(ref.handset)];
    const auto bit = static_cast<uint16_t>(1u << ref.key);
    if (pressed) {
        keys.fetch_or(bit, std::memory_order_relaxed);
    } else {
        keys.fetch_and(static_cast<uint16_t>(~bit), std::memory_order_relaxed);
    }
}

}  // namespace beebium
