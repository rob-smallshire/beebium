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

#ifndef BEEBIUM_LINUX_PRESET_STARTUP_LINKS_HPP
#define BEEBIUM_LINUX_PRESET_STARTUP_LINKS_HPP

#include "preset/PresetSchema.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace beebium::linux_client {

// The startup links' layout, as describe-preset-schema gives it: which bits
// hold the screen mode and the boot action, and every link's name and
// meaning. Nothing about the byte is the client's own.
struct LinksLayout {
    struct Bit {
        int bit = 0;
        std::string link;
        std::string label;
        std::string description;
    };
    std::vector<Bit> bits;              // as the schema lists them, link 1 first
    std::vector<int> screen_mode_bits;  // least significant first
    int auto_boot_bit = 3;
    bool auto_boot_when_clear = true;
    bool link_made_when_clear = true;
    std::uint8_t default_byte = 255;
    int default_screen_mode = 7;
    bool default_auto_boot = false;

    static std::optional<LinksLayout> from_schema(const PresetSchema& schema);
};

// One byte, seen four ways: the screen mode and auto boot, each link made
// or open, hex and decimal (docs/preset-schema.md, startup). Written as the
// screen mode and auto boot while every other link is at its default, and
// as the whole links byte once one is not, as the macOS client writes it.
class StartupLinks {
public:
    static StartupLinks from_json(const LinksLayout& layout, const nlohmann::ordered_json& startup);

    std::uint8_t byte() const { return byte_; }
    int screen_mode() const;
    bool auto_boot() const;
    bool link_made(int bit) const;
    std::string hex() const;

    void set_screen_mode(int mode);
    void set_auto_boot(bool on);
    void set_link_made(int bit, bool made);
    // Whole-byte entries; false (and no change) when out of range.
    bool set_hex(const std::string& text);
    bool set_decimal(const std::string& text);

    nlohmann::ordered_json to_json() const;

private:
    void set_bit(int bit, bool set);
    bool bit_set(int bit) const { return (byte_ >> bit) & 1; }

    LinksLayout layout_;
    std::uint8_t byte_ = 255;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_PRESET_STARTUP_LINKS_HPP
