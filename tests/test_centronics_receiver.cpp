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


#include <catch2/catch_test_macros.hpp>
#include <beebium/printer/CentronicsReceiver.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

using namespace beebium::printer;
using namespace std::chrono_literals;

namespace {

// Drive one strobe: the falling edge at this call, with the given data.
// Returns the /ACK level the receiver drove during that call.
uint8_t strobe(CentronicsReceiver& rx, uint8_t data) {
    rx.update_control_lines(1, data);
    return rx.update_control_lines(0, data);
}

// Tick with /STROBE back high, returning the /ACK levels seen, one per tick.
std::vector<uint8_t> idle_ticks(CentronicsReceiver& rx, int count, uint8_t data = 0xFF) {
    std::vector<uint8_t> ack;
    for (int i = 0; i < count; ++i) {
        ack.push_back(rx.update_control_lines(1, data));
    }
    return ack;
}

std::string drain(CentronicsReceiver& rx) {
    std::string out;
    uint8_t byte;
    while (rx.try_dequeue(byte)) {
        out.push_back(static_cast<char>(byte));
    }
    return out;
}

}  // namespace

TEST_CASE("CentronicsReceiver acknowledges a strobe after the configured delay", "[centronics]") {
    CentronicsReceiver::Options options;
    options.ack_delay_ticks = 4;
    options.ack_pulse_ticks = 4;
    CentronicsReceiver rx(options);

    REQUIRE(idle_ticks(rx, 3) == std::vector<uint8_t>{1, 1, 1});

    // The strobe tick itself: the byte is accepted, /ACK still high
    REQUIRE(strobe(rx, 'H') == 1);
    REQUIRE(rx.bytes_received() == 1);

    // Three more high ticks, then four low, then high again
    REQUIRE(idle_ticks(rx, 10) == std::vector<uint8_t>{1, 1, 1, 0, 0, 0, 0, 1, 1, 1});
    REQUIRE(drain(rx) == "H");
}

TEST_CASE("CentronicsReceiver with no delay asserts /ACK on the strobe tick", "[centronics]") {
    CentronicsReceiver::Options options;
    options.ack_delay_ticks = 0;
    options.ack_pulse_ticks = 2;
    CentronicsReceiver rx(options);
    REQUIRE(strobe(rx, 'A') == 0);
    REQUIRE(idle_ticks(rx, 3) == std::vector<uint8_t>{0, 1, 1});
}

TEST_CASE("CentronicsReceiver latches the data at the strobe edge, not at acknowledge", "[centronics]") {
    CentronicsReceiver rx;
    strobe(rx, 'X');
    // The lines change before the acknowledge arrives; the latched byte stands
    idle_ticks(rx, 12, 'Y');
    REQUIRE(drain(rx) == "X");
}

TEST_CASE("CentronicsReceiver ignores a held strobe and a rising edge", "[centronics]") {
    CentronicsReceiver rx;
    strobe(rx, 'A');
    for (int i = 0; i < 20; ++i) rx.update_control_lines(0, 'B');  // still low: no new edge
    idle_ticks(rx, 12, 'C');                                        // rising edge: nothing
    REQUIRE(rx.bytes_received() == 1);
    REQUIRE(drain(rx) == "A");
}

TEST_CASE("CentronicsReceiver offline accepts nothing and never acknowledges", "[centronics]") {
    CentronicsReceiver::Options options;
    options.online = false;
    CentronicsReceiver rx(options);
    REQUIRE_FALSE(rx.is_online());

    REQUIRE(strobe(rx, 'Q') == 1);
    auto ack = idle_ticks(rx, 50);
    REQUIRE(std::count(ack.begin(), ack.end(), 0) == 0);
    REQUIRE(rx.bytes_received() == 0);
    REQUIRE(drain(rx).empty());

    SECTION("A strobe arriving once online is accepted and acknowledged") {
        rx.set_online(true);
        REQUIRE(rx.is_online());
        strobe(rx, 'R');
        ack = idle_ticks(rx, 12);
        REQUIRE(std::count(ack.begin(), ack.end(), 0) == 4);
        REQUIRE(drain(rx) == "R");
    }
}

TEST_CASE("CentronicsReceiver holds the byte when full and acknowledges once room appears", "[centronics]") {
    CentronicsReceiver::Options options;
    options.queue_depth = 2;
    options.ack_delay_ticks = 1;
    options.ack_pulse_ticks = 1;
    CentronicsReceiver rx(options);

    strobe(rx, '1');
    idle_ticks(rx, 4);
    strobe(rx, '2');
    idle_ticks(rx, 4);
    REQUIRE(rx.bytes_received() == 2);
    REQUIRE(rx.bytes_queued() == 2);

    // Third byte: no room, so it is held and no acknowledge comes
    REQUIRE(strobe(rx, '3') == 1);
    auto ack = idle_ticks(rx, 100);
    REQUIRE(std::count(ack.begin(), ack.end(), 0) == 0);
    REQUIRE(rx.bytes_received() == 2);

    // One dequeue makes room: the held byte goes in and is acknowledged
    uint8_t byte = 0;
    REQUIRE(rx.try_dequeue(byte));
    REQUIRE(byte == '1');
    ack = idle_ticks(rx, 4);
    REQUIRE(ack == std::vector<uint8_t>{1, 0, 1, 1});
    REQUIRE(rx.bytes_received() == 3);
    REQUIRE(drain(rx) == "23");
    REQUIRE(rx.bytes_queued() == 0);
}

TEST_CASE("CentronicsReceiver timed wait returns a byte or times out", "[centronics]") {
    CentronicsReceiver rx;
    uint8_t byte = 0;
    REQUIRE_FALSE(rx.wait_dequeue_timed(byte, 1ms));
    strobe(rx, 'Z');
    REQUIRE(rx.wait_dequeue_timed(byte, 1ms));
    REQUIRE(byte == 'Z');
    REQUIRE(rx.bytes_queued() == 0);
}
