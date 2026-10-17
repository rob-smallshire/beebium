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

#include "AcornRtcExtension.hpp"
#include "AcornRtcConfig.hpp"
#include "AcornRtcDispatcher.hpp"
#include "TimeParser.hpp"
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/UserPort.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace beebium {

AcornRtcExtension::AcornRtcExtension() = default;
AcornRtcExtension::~AcornRtcExtension() = default;

std::span<const std::string_view> AcornRtcExtension::attaches_to() const {
    static constexpr std::string_view deps[] = {"user-port"};
    return deps;
}

std::span<const std::string_view> AcornRtcExtension::provides() const {
    return {};
}

void AcornRtcExtension::init(ExtensionContext& ctx) {
    start_clock();

    // Create the channel dispatcher for the RTC's typed API.
    dispatcher_ = std::make_unique<AcornRtcDispatcher>(chip_);

    // Attach to User Port
    ctx.get<UserPort>().attach(*this);
}

void AcornRtcExtension::start_clock() {
    // The start time and register layout, read as the pre-launch check
    // (check_acorn_rtc_config) reads them. The framework has already refused
    // a time given with an offset (the manifest's "start" group).
    Saf3019p::DateTime target_dt;
    try {
        target_dt = acorn_rtc_start_time(config(), current_local_datetime());
        chip_.set_layout(acorn_rtc_layout(config()));
    } catch (const std::invalid_argument& e) {
        throw std::runtime_error(std::string("Acorn RTC: ") + e.what());
    }
    // Initialise the chip (validates year range for the layout)
    if (!chip_.initialise(target_dt)) {
        const auto [first_year, last_year] = Saf3019p::year_range(chip_.layout());
        const std::string range = std::to_string(first_year) + "-" + std::to_string(last_year);
        throw std::runtime_error(
            "Acorn RTC: year " + std::to_string(target_dt.year) +
            " cannot be represented (valid range: " + range + " for layout '" +
            std::string(*config_value("layout")) + "').\n"
            "  Use --acorn-rtc time=YYYY-MM-DDThhmm with a date in range,\n"
            "  or use --acorn-rtc offset=-Ny to shift the clock back.");
    }

    std::cout << "Acorn RTC: initialised to "
              << target_dt.year << "-"
              << (target_dt.month < 10 ? "0" : "") << target_dt.month << "-"
              << (target_dt.day < 10 ? "0" : "") << target_dt.day << "T"
              << (target_dt.hour < 10 ? "0" : "") << target_dt.hour << ":"
              << (target_dt.minute < 10 ? "0" : "") << target_dt.minute << "\n";
}

std::string AcornRtcExtension::ClockUnit::hardware_descriptor() const {
    // The layout says what the registers mean, so contents kept under one
    // are not restored under another.
    return "Acorn RTC: SAF3019P, layout " +
           std::string(extension_.config_value("layout").value_or(""));
}

std::vector<uint8_t> AcornRtcExtension::ClockUnit::serialise() const {
    const auto registers = extension_.chip_.battery_image();
    std::vector<uint8_t> contents(registers.begin(), registers.end());
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    for (int i = 0; i < 8; ++i) {
        contents.push_back(static_cast<uint8_t>(static_cast<std::uint64_t>(now) >> (8 * i)));
    }
    contents.push_back(static_cast<uint8_t>(extension_.chip_.accumulated_seconds()));
    return contents;
}

bool AcornRtcExtension::ClockUnit::restore(std::span<const uint8_t> contents) {
    std::array<uint8_t, 8> registers{};
    if (contents.size() != registers.size() + 8 + 1) return false;
    std::copy_n(contents.begin(), registers.size(), registers.begin());
    std::uint64_t kept = 0;
    for (int i = 0; i < 8; ++i) {
        kept |= static_cast<std::uint64_t>(contents[registers.size() + i]) << (8 * i);
    }
    const int prescaler = contents[registers.size() + 8];
    if (prescaler >= 60) return false;

    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const std::int64_t since = std::max<std::int64_t>(0, now - static_cast<std::int64_t>(kept));
    const std::int64_t seconds = prescaler + since;
    extension_.chip_.load_battery_image(registers);
    extension_.chip_.advance_by_minutes(static_cast<int>(seconds / 60));
    extension_.chip_.set_accumulated_seconds(static_cast<int>(seconds % 60));
    return true;
}

void AcornRtcExtension::shutdown() {
    dispatcher_.reset();
}

std::vector<ExtensionRpcDispatcher*> AcornRtcExtension::rpc_dispatchers() {
    if (dispatcher_) {
        return {dispatcher_.get()};
    }
    return {};
}

uint8_t AcornRtcExtension::update_port_b(uint8_t output, uint8_t ddr) {
    // Detect DDRB reset pattern: bits 0-2 all set to output
    bool ddr_outputs = (ddr & 0x07) == 0x07;
    if (ddr_outputs && !ddr_reset_armed_) {
        chip_.reset_protocol();
        ddr_reset_armed_ = true;
    } else if (!ddr_outputs) {
        ddr_reset_armed_ = false;
    }

    // Advance counters to current wall-clock time
    chip_.advance_counters();

    // Forward Port B output to chip (CBUS protocol)
    chip_.write(output);

    // Return input pin state:
    // PB0 = DATA from chip (read back during TIME READ)
    // PB5 = COMP (comparator output, normally low = no alarm match)
    // PB7 = POWF (power failure, high = no failure)
    // All other input pins pulled high
    uint8_t input = 0xFF;
    input = (input & 0xFE) | chip_.read_data_bit();  // PB0 = DATA
    input &= ~(1 << 5);                               // PB5 = COMP low (no alarm)
    // PB7 stays high (POWF = no power failure)
    return input;
}

}  // namespace beebium
