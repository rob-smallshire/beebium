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


#pragma once

#include "Via6522.hpp"
#include "extension/PrinterPortDevice.hpp"
#include "extension/UserPortDevice.hpp"
#include "indicators/IndicatorFilter.hpp"
#include "indicators/Indicators.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace beebium {

// User VIA Peripheral - the hardware-owned composite on the User VIA.
//
// The User VIA (6522 at &FE60) in a BBC Micro connects to two separate
// external connectors on the one chip:
// - Port B and CB1/CB2: the User Port (PL10)
// - Port A and CA1/CA2: the parallel printer port (PL9); PA0-PA7 are
//   buffered outputs, CA2 is /STROBE out, CA1 is /ACK in
//
// A Via6522 takes a single ViaPeripheral, so this composite owns both
// halves and dispatches each port's traffic to its own device. Either
// half may be empty, in which case that port behaves as the
// ViaPeripheral defaults (inputs pulled high, control lines untouched).
//
// The port B half is a UserPortDevice attached through the UserPort
// handle. The port A half is a PrinterPortDevice attached through the
// PrinterPort handle.
//
// The composite also owns the printer activity indicator, because the
// indicator reports traffic at the connector: it lights on a strobe whether
// or not a device is attached, and goes out after 200 ms of emulated time
// without one. A machine stalled waiting for /ACK therefore shows a lit
// lamp until the timeout.
class UserViaPeripheral : public ViaPeripheral {
public:
    static constexpr const char* kPrinterActivityIndicatorName = "printer-activity-led";

    // Emulated time, in VIA ticks at 1 MHz, that the printer activity
    // indicator stays lit after the last strobe.
    static constexpr uint32_t kPrinterActivityTimeoutTicks = 200000;

    // A composite without an indicator registry (for tests).
    UserViaPeripheral() = default;

    // A composite that registers the printer activity indicator with the
    // machine's registry. Must be constructed before Indicators::start().
    explicit UserViaPeripheral(Indicators& indicators)
        : indicators_(&indicators) {
        std::unordered_map<std::string, std::string> metadata;
        metadata.emplace("label", "PRINTER");
        metadata.emplace("color", "568nm");
        metadata.emplace("shape", "rectangular");
        printer_activity_led_id_ = indicators_->register_indicator(
            kPrinterActivityIndicatorName, std::make_unique<PassthroughFilter>(),
            std::move(metadata));
    }

    // Attach a device to the User Port half (Port B, CB1, CB2).
    // Throws std::runtime_error if a device is already attached.
    void attach_user_port_device(UserPortDevice& device) {
        if (user_port_device_) {
            throw std::runtime_error(
                "UserPort: a device is already attached (only one User Port device is supported)");
        }
        user_port_device_ = &device;
    }

    bool has_user_port_device() const { return user_port_device_ != nullptr; }

    // Attach a device to the printer port half (Port A, CA1, CA2).
    // Throws std::runtime_error if a device is already attached.
    void attach_printer_port_device(PrinterPortDevice& device) {
        if (printer_port_device_) {
            throw std::runtime_error(
                "PrinterPort: a device is already attached (only one printer port device is supported)");
        }
        printer_port_device_ = &device;
        printer_port_device_->update_data_lines(data_lines_);
    }

    bool has_printer_port_device() const { return printer_port_device_ != nullptr; }

    uint8_t update_port_a(uint8_t output, uint8_t ddr) override {
        return port_a_update(output, ddr);
    }

    uint8_t update_port_b(uint8_t output, uint8_t ddr) override {
        if (!user_port_device_) {
            return ViaPeripheral::update_port_b(output, ddr);
        }
        return user_port_device_->update_port_b(output, ddr);
    }

    void update_control_lines(uint8_t& ca1, uint8_t& ca2,
                              uint8_t& cb1, uint8_t& cb2) override {
        port_a_control_lines(ca1, ca2);
        if (user_port_device_) {
            user_port_device_->update_control_lines(cb1, cb2);
        }
    }

private:
    // Port A half: the printer port. The 74LS244 buffer makes the lines
    // output only, so the VIA's port A inputs always read high, and bits
    // the DDR leaves as input float high at the connector.
    uint8_t port_a_update(uint8_t output, uint8_t ddr) {
        uint8_t data = static_cast<uint8_t>(output | ~ddr);
        if (data != data_lines_) {
            data_lines_ = data;
            if (printer_port_device_) {
                printer_port_device_->update_data_lines(data_lines_);
            }
        }
        return ViaPeripheral::update_port_a(output, ddr);
    }

    // ca2 is /STROBE as the VIA drives it; ca1 is /ACK, which the VIA resets
    // to 1 after every tick, so the device's level is written on every call.
    void port_a_control_lines(uint8_t& ca1, uint8_t& ca2) {
        update_printer_activity(ca2);
        if (printer_port_device_) {
            ca1 = printer_port_device_->update_control_lines(ca2, data_lines_);
        }
    }

    void update_printer_activity(uint8_t strobe) {
        bool falling_edge = (strobe_level_ != 0) && (strobe == 0);
        strobe_level_ = strobe;
        if (falling_edge) {
            printer_activity_ticks_remaining_ = kPrinterActivityTimeoutTicks;
            set_printer_activity_led(true);
        } else if (printer_activity_ticks_remaining_ > 0) {
            --printer_activity_ticks_remaining_;
            if (printer_activity_ticks_remaining_ == 0) {
                set_printer_activity_led(false);
            }
        }
    }

    void set_printer_activity_led(bool lit) {
        if (indicators_ && lit != printer_activity_led_lit_) {
            indicators_->set(printer_activity_led_id_, lit ? 255 : 0);
        }
        printer_activity_led_lit_ = lit;
    }

    // Port B half: the User Port device, if one is attached.
    UserPortDevice* user_port_device_ = nullptr;

    // Port A half: the printer port device, if one is attached, and the
    // data lines as last presented at the connector (all input at reset,
    // so floating high).
    PrinterPortDevice* printer_port_device_ = nullptr;
    uint8_t data_lines_ = 0xFF;

    // Printer activity indicator (optional)
    Indicators* indicators_ = nullptr;
    uint16_t printer_activity_led_id_ = 0;
    uint8_t strobe_level_ = 1;
    uint32_t printer_activity_ticks_remaining_ = 0;
    bool printer_activity_led_lit_ = false;
};

} // namespace beebium
