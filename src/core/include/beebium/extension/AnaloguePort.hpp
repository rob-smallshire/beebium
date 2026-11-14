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

#ifndef BEEBIUM_EXTENSION_ANALOGUE_PORT_HPP
#define BEEBIUM_EXTENSION_ANALOGUE_PORT_HPP

#include "AnaloguePortDevice.hpp"
#include "Export.hpp"
#include "../devices/Upd7002.hpp"

#include <concepts>

namespace beebium {

class SystemViaPeripheral;

// Port handle for the BBC Micro's 15-way analogue port.
//
// A thin handle over two hardware-owned parts: the SystemViaPeripheral,
// which reads the device's button lines into PB4 and PB5 and its light pen
// strobe into CB2 (and passes the strobe to the CRTC), and the uPD7002,
// which samples the device's channel levels at the start of each
// conversion. The converter knows nothing of extensions: the handle gives it
// an AnalogueInputSource that forwards to the device.
//
// Only one device can be attached at a time -- the port is a single
// connector. Attempting to attach a second device throws.
class BEEBIUM_EXT_API AnaloguePort {
public:
    AnaloguePort(SystemViaPeripheral& system_via_peripheral, Upd7002& converter);

    AnaloguePort(const AnaloguePort&) = delete;
    AnaloguePort& operator=(const AnaloguePort&) = delete;

    // Attach a device to the analogue port.
    // Throws std::runtime_error if a device is already attached.
    void attach(AnaloguePortDevice& device);

    // Query whether a device is currently attached.
    bool is_occupied() const;

private:
    // The converter's view of the attached device's channel inputs.
    class DeviceInputs final : public AnalogueInputSource {
    public:
        AnaloguePortDevice* device = nullptr;
        uint16_t level(int channel) override { return device->channel_level(channel); }
    };

    SystemViaPeripheral& system_via_peripheral_;
    Upd7002& converter_;
    DeviceInputs device_inputs_;
};

// Concept to detect hardware that has an analogue port.
template<typename T>
concept HasAnaloguePort = requires(T& hw) {
    { hw.analogue_port() } -> std::convertible_to<AnaloguePort&>;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_ANALOGUE_PORT_HPP
