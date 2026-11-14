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

#include "beebium/extension/AnaloguePort.hpp"
#include "beebium/SystemViaPeripheral.hpp"

namespace beebium {

AnaloguePort::AnaloguePort(SystemViaPeripheral& system_via_peripheral, Upd7002& converter)
    : system_via_peripheral_(system_via_peripheral), converter_(converter) {}

void AnaloguePort::attach(AnaloguePortDevice& device) {
    system_via_peripheral_.attach_analogue_port_device(device);
    device_inputs_.device = &device;
    converter_.set_input_source(&device_inputs_);
}

bool AnaloguePort::is_occupied() const {
    return system_via_peripheral_.has_analogue_port_device();
}

}  // namespace beebium
