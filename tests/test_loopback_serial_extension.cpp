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

#include "LoopbackSerialExtension.hpp"

#include <beebium/devices/Mc6850.hpp>
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/SerialPort.hpp>
#include <beebium/serial/SerialSocket.hpp>
#include <beebium/serial/SerialUla.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using namespace beebium;

namespace {
// 8N1 at the /64 counter divide, which is what MOS 1.20 uses on RS423: the
// Serial ULA supplies 64 receive and transmit clocks per bit at the selected rate.
constexpr uint8_t CONTROL_8N1 =
    Mc6850::COUNTER_DIVIDE_64 | (0x05 << Mc6850::CR_WORD_SELECT_SHIFT);
}  // namespace

TEST_CASE("LoopbackSerialExtension attaches and echoes a transmitted byte",
          "[serial][loopback-serial]") {
    SerialSocket socket;
    SerialPort port(socket);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, &port);

    LoopbackSerialExtension ext;
    REQUIRE(ext.attaches_to().size() == 1);
    CHECK(ext.attaches_to()[0] == "serial-port");

    ext.init(ctx);
    REQUIRE(port.is_occupied());

    // Transmit a byte; loopback should echo it back into the receiver.
    socket.write_acia(0, CONTROL_8N1);
    socket.write_ula(0, SerialUla::RS423_SELECT);
    socket.write_acia(1, 0xC3);
    for (int i = 0; i < 20000 && (socket.read_acia(0) & Mc6850::SR_RDRF) == 0; ++i) {
        socket.tick_rising();
        socket.tick_falling();
    }
    REQUIRE((socket.read_acia(0) & Mc6850::SR_RDRF) != 0);
    CHECK(socket.read_acia(1) == 0xC3);

    ext.shutdown();
}
