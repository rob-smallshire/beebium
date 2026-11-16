// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
// Copyright 2026 Mark J. Fisher
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

// POSIX-only: exercises the host serial transport (PtyMaster + HostSerialEndpoint)
// bridged through the SerialSocket, with the far end driven by a PosixSerialPort
// opened on the pty slave -- the same OS I/O path a real serial client uses.

#ifndef _WIN32

#include "HostSerialEndpoint.hpp"

#include <beebium/serial/PosixSerialPort.hpp>
#include <beebium/serial/PtyMaster.hpp>
#include <beebium/serial/SerialSocket.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

using namespace beebium;
using namespace beebium::serial;

namespace {

// 8N1 at the /64 counter divide, which is what MOS 1.20 uses on RS423: the
// Serial ULA supplies 64 receive and transmit clocks per bit at the selected rate.
constexpr uint8_t CONTROL_8N1 = Mc6850::COUNTER_DIVIDE_64
    | (0x05 << Mc6850::CR_WORD_SELECT_SHIFT);

void tick(SerialSocket& socket, int n) {
    for (int i = 0; i < n; ++i) {
        socket.tick_rising();
        socket.tick_falling();
    }
}

}  // namespace

TEST_CASE("PtyMaster opens and exposes a slave path", "[serial][pty]") {
    PtyMaster pty;
    REQUIRE(pty.is_open());
    CHECK_FALSE(pty.slave_path().empty());
    CHECK(pty.advertised_path() == pty.slave_path());  // no symlink requested
}

TEST_CASE("Serial socket transmits over a pty to a serial client", "[serial][pty]") {
    auto pty = std::make_unique<PtyMaster>();
    REQUIRE(pty->is_open());
    PosixSerialPort client(pty->slave_path(), 19200);  // the "device" end
    REQUIRE(client.is_open());

    SerialSocket socket;
    auto endpoint = std::make_shared<HostSerialEndpoint>(std::move(pty));
    socket.set_device(endpoint.get());

    socket.write_acia(0, CONTROL_8N1);
    socket.write_ula(0, SerialUla::RS423_SELECT);  // 19200, carrier present
    socket.write_acia(1, 0x5A);                     // transmit 'Z'

    // Serialise the character, then poll the client end for the byte.
    bool got = false;
    uint8_t value = 0;
    for (int attempt = 0; attempt < 20 && !got; ++attempt) {
        tick(socket, 600);
        std::array<std::uint8_t, 16> buf{};
        ReadResult r = client.read(std::span<std::uint8_t>(buf.data(), buf.size()));
        if (r.bytes > 0) {
            value = buf[0];
            got = true;
        }
    }
    REQUIRE(got);
    CHECK(value == 0x5A);
}

TEST_CASE("Serial socket receives from a serial client over a pty", "[serial][pty]") {
    auto pty = std::make_unique<PtyMaster>();
    REQUIRE(pty->is_open());
    PosixSerialPort client(pty->slave_path(), 19200);
    REQUIRE(client.is_open());

    SerialSocket socket;
    auto endpoint = std::make_shared<HostSerialEndpoint>(std::move(pty));
    socket.set_device(endpoint.get());

    socket.write_acia(0, CONTROL_8N1);
    socket.write_ula(0, SerialUla::RS423_SELECT);

    // The device end writes a byte; the endpoint's reader thread picks it up,
    // and the ULA shifts it into the ACIA.
    std::array<std::uint8_t, 1> out{0x39};
    client.write(std::span<const std::uint8_t>(out.data(), out.size()));

    bool rdrf = false;
    for (int attempt = 0; attempt < 30 && !rdrf; ++attempt) {
        tick(socket, 400);
        if (socket.read_acia(0) & Mc6850::SR_RDRF) {
            rdrf = true;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    REQUIRE(rdrf);
    CHECK(socket.read_acia(1) == 0x39);
}

#endif  // !_WIN32
