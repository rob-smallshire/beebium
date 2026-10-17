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

#ifndef BEEBIUM_ACORN_RTC_EXTENSION_HPP
#define BEEBIUM_ACORN_RTC_EXTENSION_HPP

#include "AcornRtcUi.hpp"
#include "Saf3019p.hpp"
#include <beebium/extension/ExtensionPersistence.hpp>
#include <beebium/extension/PeripheralExtension.hpp>
#include <beebium/extension/UserPortDevice.hpp>

#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace beebium {

class ExtensionRpcDispatcher;
class AcornRtcDispatcher;  // defined in AcornRtcDispatcher.hpp

class AcornRtcExtension : public PeripheralExtension,
                              public UserPortDevice,
                              public ExtensionPersistence {
public:
    // Both defined in the .cpp where AcornRtcDispatcher is complete (the
    // unique_ptr<AcornRtcDispatcher> member needs it for construction cleanup
    // and destruction).
    AcornRtcExtension();
    ~AcornRtcExtension() override;

    // PeripheralExtension interface
    std::span<const std::string_view> attaches_to() const override;
    std::span<const std::string_view> provides() const override;
    void init(ExtensionContext& ctx) override;
    void shutdown() override;
    std::vector<ExtensionRpcDispatcher*> rpc_dispatchers() override;

    // ExtensionUi panel for the Peripherals sidebar. Returns a stable
    // pointer (owned by this extension) showing the current chip
    // time and active register layout.
    ExtensionUi* ui() override { return &ui_; }

    // The clock a battery keeps running while the machine is off: in a
    // saved machine it carries on from where it was.
    ExtensionPersistence* persistence() override { return this; }
    std::vector<BatteryBackedUnit*> battery_units() override { return {&clock_unit_}; }

    // UserPortDevice interface
    uint8_t update_port_b(uint8_t output, uint8_t ddr) override;

    // Access for the dispatcher and UI.
    Saf3019p& chip() { return chip_; }

private:
    // Sets the chip's layout and time from the configuration: the clock as
    // it is when its battery is first fitted.
    void start_clock();

    // The chip's registers, then when they were kept (seconds since the
    // epoch, little-endian two's complement, 8 bytes) and the seconds the
    // prescaler had counted (1 byte). Restored, the clock runs on by the
    // time since, as it would have on its battery.
    class ClockUnit final : public BatteryBackedUnit {
    public:
        explicit ClockUnit(AcornRtcExtension& extension) : extension_(extension) {}
        ClockUnit(const ClockUnit&) = delete;
        ClockUnit& operator=(const ClockUnit&) = delete;

        std::string_view name() const override { return "saf3019p"; }
        std::uint32_t layout_version() const override { return 1; }
        std::string hardware_descriptor() const override;
        void seed() override { extension_.start_clock(); }
        std::vector<uint8_t> serialise() const override;
        bool restore(std::span<const uint8_t> contents) override;

    private:
        AcornRtcExtension& extension_;
    };

    Saf3019p chip_;
    ClockUnit clock_unit_{*this};
    bool ddr_reset_armed_ = false;
    std::unique_ptr<AcornRtcDispatcher> dispatcher_;
    AcornRtcUi ui_{*this};
};

}  // namespace beebium

#endif  // BEEBIUM_ACORN_RTC_EXTENSION_HPP
