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

// An MC146818 clock kept running by a battery, as a battery-backed unit: its
// battery-backed registers and CMOS RAM, then how far it is set from host
// time (seconds, little-endian two's complement, 8 bytes). The board that
// fits the chip gives the unit its name, its hardware descriptor and the
// seed its clock starts from.

#pragma once

#include "beebium/BatteryBackedUnit.hpp"
#include "beebium/devices/Mc146818Rtc.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace beebium {

class Mc146818BatteryUnit final : public BatteryBackedUnit {
public:
    using Seed = std::function<void(Mc146818Rtc&)>;

    Mc146818BatteryUnit(Mc146818Rtc& rtc, std::string name, std::string hardware_descriptor,
                        Seed seed)
        : rtc_(rtc),
          name_(std::move(name)),
          hardware_descriptor_(std::move(hardware_descriptor)),
          seed_(std::move(seed)) {}
    Mc146818BatteryUnit(const Mc146818BatteryUnit&) = delete;
    Mc146818BatteryUnit& operator=(const Mc146818BatteryUnit&) = delete;

    std::string_view name() const override { return name_; }
    std::uint32_t layout_version() const override { return 1; }
    std::string hardware_descriptor() const override { return hardware_descriptor_; }

    void seed() override {
        seed_(rtc_);
        rtc_.set_clock_offset(std::chrono::seconds(0));
        restored_ = false;
    }

    std::vector<std::uint8_t> serialise() const override {
        const auto image = rtc_.battery_image();
        std::vector<std::uint8_t> contents(image.begin(), image.end());
        auto offset = static_cast<std::uint64_t>(rtc_.clock_offset().count());
        for (int i = 0; i < OFFSET_SIZE; ++i) {
            contents.push_back(static_cast<std::uint8_t>(offset >> (8 * i)));
        }
        return contents;
    }

    bool restore(std::span<const std::uint8_t> contents) override {
        Mc146818Rtc::BatteryImage image{};
        if (contents.size() != image.size() + OFFSET_SIZE) return false;
        std::copy_n(contents.begin(), image.size(), image.begin());
        std::uint64_t offset = 0;
        for (int i = 0; i < OFFSET_SIZE; ++i) {
            offset |= static_cast<std::uint64_t>(contents[image.size() + i]) << (8 * i);
        }
        rtc_.load_battery_image(image);
        rtc_.set_clock_offset(std::chrono::seconds(static_cast<std::int64_t>(offset)));
        restored_ = true;
        return true;
    }

    // Whether the clock holds what a saved machine kept, rather than the seed.
    bool restored() const { return restored_; }

private:
    static constexpr int OFFSET_SIZE = 8;

    Mc146818Rtc& rtc_;
    std::string name_;
    std::string hardware_descriptor_;
    Seed seed_;
    bool restored_ = false;
};

}  // namespace beebium
