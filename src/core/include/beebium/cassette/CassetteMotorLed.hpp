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
// latch bit 7, whether or not a tape is in. It registers in `indicators` (before
// they start) as cassette-motor-led when constructed, so a hardware policy
// declares it after the System VIA peripheral (which registers the lock LEDs)
// and before the disc drives: the status bar then lists it between SHIFT LOCK
// and FDD 0, as the storage sidebar puts the cassette above the floppies.
// attach() connects it to the Serial ULA, which a policy declares later. The
// MOS's 1 ms motor blips pass through unfiltered; a client applies whatever
// hold-off it gives the drive LEDs.
class CassetteMotorLed {
public:
    explicit CassetteMotorLed(Indicators& indicators) : indicators_(indicators) {
        std::unordered_map<std::string, std::string> metadata;
        metadata.emplace("label", "CASSETTE MOTOR");
        metadata.emplace("color", "625nm");
        metadata.emplace("shape", "domed");
        id_ = indicators.register_indicator(
            "cassette-motor-led", std::make_unique<PassthroughFilter>(), std::move(metadata));
    }

    // Light the LED with the ULA's motor relay.
    void attach(SerialUla& ula) {
        ula.set_motor_listener([&indicators = indicators_, id = id_](bool on) { indicators.set(id, on ? 255 : 0); });
    }

    uint16_t id() const { return id_; }

private:
    Indicators& indicators_;
    uint16_t id_ = 0;
};

}  // namespace beebium
