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

#include "keyboard/KeyboardClasses.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <sstream>

namespace beebium::linux_client {

namespace {

constexpr std::uint16_t PI_400_VENDOR = 0x04d9;
constexpr std::uint16_t PI_400_PRODUCT = 0x0007;

std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// The value after `key=` in a line of space-separated fields.
std::string_view field(std::string_view line, std::string_view key) {
    const std::size_t at = line.find(key);
    if (at == std::string_view::npos) {
        return {};
    }
    std::string_view rest = line.substr(at + key.size());
    return rest.substr(0, rest.find(' '));
}

std::uint16_t hex16(std::string_view text) {
    unsigned value = 0;
    std::from_chars(text.data(), text.data() + text.size(), value, 16);
    return static_cast<std::uint16_t>(value);
}

// Whether a KEY capability bitmap, the kernel's words most significant
// first, has the top letter row (Q to P, keys 16 to 25) and A (key 30): all
// in the last word whatever the kernel's word size.
bool has_letter_keys(std::string_view bitmap) {
    const std::size_t space = bitmap.find_last_of(' ');
    const std::string_view last = space == std::string_view::npos ? bitmap : bitmap.substr(space + 1);
    unsigned long long word = 0;
    std::from_chars(last.data(), last.data() + last.size(), word, 16);
    constexpr unsigned long long LETTERS = (0x3ffull << 16) | (1ull << 30);
    return (word & LETTERS) == LETTERS;
}

bool served_by_kbd(std::string_view handlers) {
    std::istringstream words{std::string(handlers)};
    std::string word;
    while (words >> word) {
        if (word == "kbd") {
            return true;
        }
    }
    return false;
}

}  // namespace

std::string keyboard_class_of(std::string_view name, std::uint16_t vendor, std::uint16_t product) {
    const std::string folded = lower(name);
    const bool named = folded.find("raspberry pi") != std::string::npos &&
                       folded.find("internal keyboard") != std::string::npos;
    if (named || (vendor == PI_400_VENDOR && product == PI_400_PRODUCT)) {
        return "pi-internal";
    }
    return "standard";
}

std::vector<InputKeyboard> keyboards_in_input_devices(std::string_view devices_text) {
    std::vector<InputKeyboard> keyboards;
    InputKeyboard device;
    std::string handlers;
    bool letters = false;
    const auto finish = [&] {
        if (served_by_kbd(handlers) && letters) {
            device.keyboard_class = keyboard_class_of(device.name, device.vendor, device.product);
            keyboards.push_back(device);
        }
        device = {};
        handlers.clear();
        letters = false;
    };
    std::size_t start = 0;
    while (start <= devices_text.size()) {
        const std::size_t end = std::min(devices_text.find('\n', start), devices_text.size());
        const std::string_view line = devices_text.substr(start, end - start);
        if (line.empty()) {
            finish();
        } else if (line.rfind("I:", 0) == 0) {
            device.vendor = hex16(field(line, "Vendor="));
            device.product = hex16(field(line, "Product="));
        } else if (line.rfind("N: Name=", 0) == 0) {
            std::string_view name = line.substr(8);
            if (name.size() >= 2 && name.front() == '"' && name.back() == '"') {
                name = name.substr(1, name.size() - 2);
            }
            device.name = std::string(name);
        } else if (line.rfind("H: Handlers=", 0) == 0) {
            handlers = std::string(line.substr(12));
        } else if (line.rfind("B: KEY=", 0) == 0) {
            letters = has_letter_keys(line.substr(7));
        }
        start = end + 1;
    }
    finish();
    return keyboards;
}

KeyboardClasses keyboard_classes(const std::vector<InputKeyboard>& keyboards) {
    KeyboardClasses classes;
    for (const InputKeyboard& keyboard : keyboards) {
        classes.insert(keyboard.keyboard_class);
    }
    if (classes.empty()) {
        classes.insert("standard");
    }
    return classes;
}

std::string keyboards_description(const std::vector<InputKeyboard>& keyboards) {
    if (keyboards.empty()) {
        return "none found, taken as standard";
    }
    std::string text;
    for (const InputKeyboard& keyboard : keyboards) {
        char ids[16];
        std::snprintf(ids, sizeof ids, "%04x:%04x", keyboard.vendor, keyboard.product);
        if (!text.empty()) {
            text += ", ";
        }
        text += keyboard.keyboard_class + " \"" + keyboard.name + "\" (" + ids + ")";
    }
    return text;
}

}  // namespace beebium::linux_client
