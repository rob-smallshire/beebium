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

#include "preset/StartupLinks.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace beebium::linux_client {

using nlohmann::ordered_json;

std::optional<LinksLayout> LinksLayout::from_schema(const PresetSchema& schema) {
    const ordered_json* section = schema.section("startup");
    if (!section || !section->contains("links")) {
        return std::nullopt;
    }
    const ordered_json& links = (*section)["links"];
    LinksLayout layout;
    for (const auto& b : links.value("bits", ordered_json::array())) {
        layout.bits.push_back({b.value("bit", 0), b.value("link", std::string()),
                               b.value("label", std::string()),
                               b.value("description", std::string())});
    }
    for (const auto& bit : links.value("screen_mode_bits", ordered_json::array())) {
        layout.screen_mode_bits.push_back(bit.get<int>());
    }
    layout.auto_boot_bit = links.value("auto_boot_bit", 3);
    layout.auto_boot_when_clear = links.value("auto_boot_true_when", std::string("clear")) == "clear";
    layout.link_made_when_clear = links.value("link_made_when", std::string("clear")) == "clear";
    layout.default_byte = static_cast<std::uint8_t>(links.value("default", 255));
    if (section->contains("screen_mode")) {
        layout.default_screen_mode = (*section)["screen_mode"].value("default", 7);
    }
    if (section->contains("auto_boot")) {
        layout.default_auto_boot = (*section)["auto_boot"].value("default", false);
    }
    return layout;
}

StartupLinks StartupLinks::from_json(const LinksLayout& layout, const ordered_json& startup) {
    StartupLinks links;
    links.layout_ = layout;
    links.byte_ = layout.default_byte;
    if (startup.is_object() && startup.contains("links") && startup["links"].is_number_integer()) {
        links.byte_ = static_cast<std::uint8_t>(startup["links"].get<int>());
        return links;
    }
    const bool have = startup.is_object();
    links.set_screen_mode(have && startup.contains("screen_mode") &&
                                  startup["screen_mode"].is_number_integer()
                              ? startup["screen_mode"].get<int>()
                              : layout.default_screen_mode);
    links.set_auto_boot(have && startup.contains("auto_boot") && startup["auto_boot"].is_boolean()
                            ? startup["auto_boot"].get<bool>()
                            : layout.default_auto_boot);
    return links;
}

void StartupLinks::set_bit(int bit, bool set) {
    if (set) {
        byte_ = static_cast<std::uint8_t>(byte_ | (1u << bit));
    } else {
        byte_ = static_cast<std::uint8_t>(byte_ & ~(1u << bit));
    }
}

int StartupLinks::screen_mode() const {
    int mode = 0;
    for (std::size_t i = 0; i < layout_.screen_mode_bits.size(); ++i) {
        if (bit_set(layout_.screen_mode_bits[i])) {
            mode |= 1 << i;
        }
    }
    return mode;
}

void StartupLinks::set_screen_mode(int mode) {
    for (std::size_t i = 0; i < layout_.screen_mode_bits.size(); ++i) {
        set_bit(layout_.screen_mode_bits[i], (mode >> i) & 1);
    }
}

bool StartupLinks::auto_boot() const {
    return bit_set(layout_.auto_boot_bit) != layout_.auto_boot_when_clear;
}

void StartupLinks::set_auto_boot(bool on) {
    set_bit(layout_.auto_boot_bit, on != layout_.auto_boot_when_clear);
}

bool StartupLinks::link_made(int bit) const {
    return bit_set(bit) != layout_.link_made_when_clear;
}

void StartupLinks::set_link_made(int bit, bool made) {
    set_bit(bit, made != layout_.link_made_when_clear);
}

std::string StartupLinks::hex() const {
    char text[3];
    std::snprintf(text, sizeof text, "%02X", byte_);
    return text;
}

bool StartupLinks::set_hex(const std::string& text) {
    std::string digits = text;
    if (!digits.empty() && (digits[0] == '&' || digits[0] == '$')) {
        digits.erase(0, 1);
    }
    if (digits.empty() || digits.size() > 2 ||
        !std::all_of(digits.begin(), digits.end(),
                     [](unsigned char c) { return std::isxdigit(c); })) {
        return false;
    }
    byte_ = static_cast<std::uint8_t>(std::stoi(digits, nullptr, 16));
    return true;
}

bool StartupLinks::set_decimal(const std::string& text) {
    if (text.empty() || text.size() > 3 ||
        !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c); })) {
        return false;
    }
    const int value = std::stoi(text);
    if (value > 255) {
        return false;
    }
    byte_ = static_cast<std::uint8_t>(value);
    return true;
}

ordered_json StartupLinks::to_json() const {
    // The friendly pair says it only while every other link is at its default.
    std::uint8_t others = 0xFF;
    for (const int bit : layout_.screen_mode_bits) {
        others = static_cast<std::uint8_t>(others & ~(1u << bit));
    }
    others = static_cast<std::uint8_t>(others & ~(1u << layout_.auto_boot_bit));
    if ((byte_ & others) == (layout_.default_byte & others)) {
        return ordered_json{{"screen_mode", screen_mode()}, {"auto_boot", auto_boot()}};
    }
    return ordered_json{{"links", byte_}};
}

}  // namespace beebium::linux_client
