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

#include "keyboard/EvdevCodes.hpp"

#include <utility>

namespace beebium::linux_client {

namespace {

// evdev code -> W3C UI Events code, after the W3C "UI Events KeyboardEvent
// code Values" specification and its Linux column. Codes beyond a typical
// PC keyboard are left out until a mapping needs them.
constexpr std::pair<std::uint32_t, std::string_view> EVDEV_TO_W3C[] = {
    {1, "Escape"},
    {2, "Digit1"}, {3, "Digit2"}, {4, "Digit3"}, {5, "Digit4"}, {6, "Digit5"},
    {7, "Digit6"}, {8, "Digit7"}, {9, "Digit8"}, {10, "Digit9"}, {11, "Digit0"},
    {12, "Minus"}, {13, "Equal"}, {14, "Backspace"}, {15, "Tab"},
    {16, "KeyQ"}, {17, "KeyW"}, {18, "KeyE"}, {19, "KeyR"}, {20, "KeyT"},
    {21, "KeyY"}, {22, "KeyU"}, {23, "KeyI"}, {24, "KeyO"}, {25, "KeyP"},
    {26, "BracketLeft"}, {27, "BracketRight"}, {28, "Enter"}, {29, "ControlLeft"},
    {30, "KeyA"}, {31, "KeyS"}, {32, "KeyD"}, {33, "KeyF"}, {34, "KeyG"},
    {35, "KeyH"}, {36, "KeyJ"}, {37, "KeyK"}, {38, "KeyL"}, {39, "Semicolon"},
    {40, "Quote"}, {41, "Backquote"}, {42, "ShiftLeft"}, {43, "Backslash"},
    {44, "KeyZ"}, {45, "KeyX"}, {46, "KeyC"}, {47, "KeyV"}, {48, "KeyB"},
    {49, "KeyN"}, {50, "KeyM"}, {51, "Comma"}, {52, "Period"}, {53, "Slash"},
    {54, "ShiftRight"}, {55, "NumpadMultiply"}, {56, "AltLeft"}, {57, "Space"},
    {58, "CapsLock"},
    {59, "F1"}, {60, "F2"}, {61, "F3"}, {62, "F4"}, {63, "F5"}, {64, "F6"},
    {65, "F7"}, {66, "F8"}, {67, "F9"}, {68, "F10"},
    {69, "NumLock"}, {70, "ScrollLock"},
    {71, "Numpad7"}, {72, "Numpad8"}, {73, "Numpad9"}, {74, "NumpadSubtract"},
    {75, "Numpad4"}, {76, "Numpad5"}, {77, "Numpad6"}, {78, "NumpadAdd"},
    {79, "Numpad1"}, {80, "Numpad2"}, {81, "Numpad3"}, {82, "Numpad0"},
    {83, "NumpadDecimal"},
    {86, "IntlBackslash"}, {87, "F11"}, {88, "F12"},
    {96, "NumpadEnter"}, {97, "ControlRight"}, {98, "NumpadDivide"},
    {99, "PrintScreen"}, {100, "AltRight"},
    {102, "Home"}, {103, "ArrowUp"}, {104, "PageUp"}, {105, "ArrowLeft"},
    {106, "ArrowRight"}, {107, "End"}, {108, "ArrowDown"}, {109, "PageDown"},
    {110, "Insert"}, {111, "Delete"},
    {119, "Pause"}, {125, "MetaLeft"}, {126, "MetaRight"}, {127, "ContextMenu"},
    {183, "F13"},
};

}  // namespace

std::string_view w3c_code_for_evdev(std::uint32_t evdev_code) {
    for (const auto& [evdev, code] : EVDEV_TO_W3C) {
        if (evdev == evdev_code) {
            return code;
        }
    }
    return {};
}

std::optional<std::uint32_t> evdev_for_w3c_code(std::string_view code) {
    for (const auto& [evdev, name] : EVDEV_TO_W3C) {
        if (name == code) {
            return evdev;
        }
    }
    return std::nullopt;
}

std::optional<std::uint32_t> evdev_for_xkb_keycode(std::uint32_t xkb_keycode) {
    if (xkb_keycode < 8) {
        return std::nullopt;
    }
    return xkb_keycode - 8;
}

}  // namespace beebium::linux_client
