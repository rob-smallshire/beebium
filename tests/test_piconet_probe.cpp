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

// Unit tests for the Piconet identification probe. These are server-free:
// they drive probe_status() against SerialPort doubles so the one/zero/many
// discovery decision (built on this probe) can be validated without hardware.

#include <catch2/catch_test_macros.hpp>

#include "beebium/econet/piconet/Mode.hpp"
#include "beebium/econet/piconet/Probe.hpp"
#include "beebium/econet/piconet/SerialPort.hpp"

#include "piconet/FakePiconetDevice.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <string_view>

using namespace beebium::piconet;
using beebium::piconet::test::FakePiconetDevice;

namespace {

// A serial port that is open, swallows writes, and never has anything to
// read: models a silent USB-CDC device that shares the Pico VID but is not
// a Piconet (or a Piconet whose firmware is wedged).
class SilentSerialPort : public SerialPort {
public:
    ReadResult read(std::span<std::uint8_t>) override {
        return ReadResult{0, /*would_block=*/true, /*error=*/false};
    }
    WriteResult write(std::span<const std::uint8_t> bytes) override {
        return WriteResult{bytes.size(), /*error=*/false};
    }
    bool is_open() const override { return true; }
    void close() override {}
    std::string_view open_error() const noexcept override { return {}; }
};

// A serial port that answers with lines that are not STATUS: a different
// device on the same VID chatting its own protocol.
class ChattySerialPort : public SerialPort {
public:
    ChattySerialPort() {
        const std::string_view reply = "HELLO world\nREADY\n";
        for (char c : reply) outgoing_.push_back(static_cast<std::uint8_t>(c));
    }
    ReadResult read(std::span<std::uint8_t> buffer) override {
        if (outgoing_.empty()) return ReadResult{0, true, false};
        std::size_t n = std::min(buffer.size(), outgoing_.size());
        for (std::size_t i = 0; i < n; ++i) {
            buffer[i] = outgoing_.front();
            outgoing_.pop_front();
        }
        return ReadResult{n, false, false};
    }
    WriteResult write(std::span<const std::uint8_t> bytes) override {
        return WriteResult{bytes.size(), false};
    }
    bool is_open() const override { return true; }
    void close() override {}
    std::string_view open_error() const noexcept override { return {}; }

private:
    std::deque<std::uint8_t> outgoing_;
};

// A closed port: is_open() false from the outset.
class ClosedSerialPort : public SerialPort {
public:
    ReadResult read(std::span<std::uint8_t>) override {
        return ReadResult{0, false, true};
    }
    WriteResult write(std::span<const std::uint8_t>) override {
        return WriteResult{0, true};
    }
    bool is_open() const override { return false; }
    void close() override {}
    std::string_view open_error() const noexcept override {
        return "not open";
    }
};

// A port that reports open but fails the STATUS write.
class WriteFailsSerialPort : public SerialPort {
public:
    ReadResult read(std::span<std::uint8_t>) override {
        return ReadResult{0, true, false};
    }
    WriteResult write(std::span<const std::uint8_t>) override {
        return WriteResult{0, /*error=*/true};
    }
    bool is_open() const override { return true; }
    void close() override {}
    std::string_view open_error() const noexcept override { return {}; }
};

}  // namespace

TEST_CASE("probe_status identifies a Piconet from its STATUS reply",
          "[piconet][probe]") {
    FakePiconetDevice device;
    device.set_firmware_version("2.0.20");

    auto status = probe_status(device, std::chrono::milliseconds(500));

    REQUIRE(status.has_value());
    CHECK(status->version_major == 2);
    CHECK(status->version_minor == 0);
    CHECK(status->version_patch == 20);
    // FakePiconetDevice defaults to station 0x02, mode Stop.
    CHECK(status->station == 0x02);
    CHECK(status->mode == Mode::Stop);
}

TEST_CASE("probe_status reflects a device with no Econet network attached",
          "[piconet][probe]") {
    // The live device replies to STATUS even with no network, which a preset
    // boot must tolerate. Model that: a Piconet answering STATUS is confirmed
    // regardless of its ADLC status register / mode.
    FakePiconetDevice device;

    auto status = probe_status(device, std::chrono::milliseconds(500));

    REQUIRE(status.has_value());
}

TEST_CASE("probe_status rejects a silent device", "[piconet][probe]") {
    SilentSerialPort port;
    auto status = probe_status(port, std::chrono::milliseconds(50));
    CHECK_FALSE(status.has_value());
}

TEST_CASE("probe_status rejects a device speaking another protocol",
          "[piconet][probe]") {
    ChattySerialPort port;
    auto status = probe_status(port, std::chrono::milliseconds(50));
    CHECK_FALSE(status.has_value());
}

TEST_CASE("probe_status rejects a closed port", "[piconet][probe]") {
    ClosedSerialPort port;
    auto status = probe_status(port, std::chrono::milliseconds(500));
    CHECK_FALSE(status.has_value());
}

TEST_CASE("probe_status rejects a port whose STATUS write fails",
          "[piconet][probe]") {
    WriteFailsSerialPort port;
    auto status = probe_status(port, std::chrono::milliseconds(500));
    CHECK_FALSE(status.has_value());
}
