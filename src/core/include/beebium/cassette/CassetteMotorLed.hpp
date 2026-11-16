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

#pragma once

#include "../indicators/IndicatorFilter.hpp"
#include "../indicators/Indicators.hpp"
#include "../serial/SerialUla.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace beebium {

// The keyboard's cassette motor LED: driven with the relay from Serial ULA
// latch bit 7, whether or not a tape is in. Registered in `indicators` (before
// they start) as cassette-motor-led, after the lock LEDs. The MOS's 1 ms motor
// blips pass through unfiltered; a client applies whatever hold-off it gives
// the drive LEDs.
inline void attach_cassette_motor_led(Indicators& indicators, SerialUla& ula) {
    std::unordered_map<std::string, std::string> metadata;
    metadata.emplace("label", "Cassette motor");
    metadata.emplace("color", "625nm");
    metadata.emplace("shape", "domed");
    const uint16_t id = indicators.register_indicator(
        "cassette-motor-led", std::make_unique<PassthroughFilter>(), std::move(metadata));
    ula.set_motor_listener([&indicators, id](bool on) { indicators.set(id, on ? 255 : 0); });
}

}  // namespace beebium
