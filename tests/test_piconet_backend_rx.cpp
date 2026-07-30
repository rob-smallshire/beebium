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

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "beebium/econet/PiconetBackend.hpp"
#include "beebium/econet/piconet/Base64.hpp"

#include "piconet/MockPiconetSerial.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

using namespace beebium;
using namespace beebium::piconet;
using beebium::piconet::test::MockPiconetSerial;

namespace {

// Poll receive_frame() until a frame arrives or timeout elapses. The
// reader thread runs asynchronously, so all RX tests need a short wait.
std::optional<NetworkFrame> wait_for_frame(PiconetBackend& backend,
                                            std::chrono::milliseconds timeout = std::chrono::milliseconds(500)) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto frame = backend.receive_frame()) {
            return frame;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return std::nullopt;
}

struct Wired {
    MockPiconetSerial* mock;
    std::unique_ptr<PiconetBackend> backend;
};

Wired make_backend(std::uint8_t initial_station = 0x02) {
    auto mock_owner = std::make_unique<MockPiconetSerial>();
    auto* mock = mock_owner.get();
    auto backend = std::make_unique<PiconetBackend>(
        PiconetConfig{"/dev/null", initial_station}, std::move(mock_owner));
    mock->clear_writes();
    return {mock, std::move(backend)};
}

}  // namespace

TEST_CASE("PiconetBackend reader thread parses RX_TRANSMIT into a Unicast NetworkFrame",
          "[piconet][backend][rx]") {
    auto w = make_backend();

    // Construct a wire-format scout and data for a Unicast from station
    // 254 (e.g. fileserver) to station 32 (us), ctrl 0x80 (no scout-extra),
    // port 0x99, payload "HELLO".
    const std::vector<std::uint8_t> scout{
        0x20, 0x00,  // dest_stn=32, dest_net=0
        0xFE, 0x00,  // src_stn=254, src_net=0
        0x80, 0x99,  // ctrl, port (high bit not set in this fixture)
    };
    const std::vector<std::uint8_t> data_frame{
        0x20, 0x00,  // dest=us
        0xFE, 0x00,  // src=them
        'H', 'E', 'L', 'L', 'O',
    };
    const std::string line =
        "RX_TRANSMIT " + encode_base64(scout) + " " + encode_base64(data_frame) + "\n";
    w.mock->stage_read_chunk(line);

    auto frame = wait_for_frame(*w.backend);
    REQUIRE(frame.has_value());
    CHECK(frame->type == FrameType::Unicast);
    CHECK(frame->dest_stn == 32);
    CHECK(frame->dest_net == 0);
    CHECK(frame->src_stn == 254);
    CHECK(frame->src_net == 0);
    // Wire ctrl 0x80 (high bit + function code 0) is stored post-mask,
    // matching FourWayHandshake's outbound convention.
    CHECK(frame->control_byte == 0x00);
    CHECK(frame->port == 0x99);
    CHECK(frame->data == std::vector<std::uint8_t>{'H', 'E', 'L', 'L', 'O'});
}

TEST_CASE("PiconetBackend reader strips the scout's high control bit",
          "[piconet][backend][rx]") {
    // Real scouts have the high bit set on the ctrl byte (it's the marker
    // distinguishing scout from data on the wire). FourWayHandshake strips
    // it before placing the value in NetworkFrame::control_byte; the
    // PiconetBackend reader must do the same when parsing RX_TRANSMIT.
    auto w = make_backend();
    const std::vector<std::uint8_t> scout{
        0x20, 0x00, 0xFE, 0x00,
        0x80 | 0x80,  // ctrl with high bit set; should be masked off
        0x99,
    };
    const std::vector<std::uint8_t> data_frame{0x20, 0x00, 0xFE, 0x00, 0xAA};
    w.mock->stage_read_chunk(
        "RX_TRANSMIT " + encode_base64(scout) + " " + encode_base64(data_frame) + "\n");

    auto frame = wait_for_frame(*w.backend);
    REQUIRE(frame.has_value());
    // Wire byte 0x80|0x80 = 0x80; after stripping the scout high bit,
    // the function code is 0x00.
    CHECK(frame->control_byte == 0x00);
}

TEST_CASE("PiconetBackend reader survives bytes split across multiple read() calls",
          "[piconet][backend][rx]") {
    // Adversarial chunking: the line is delivered as several small chunks,
    // each crossing a different boundary (mid-base64, between fields,
    // before the terminator). Partial-line buffering must hold the
    // accumulated bytes until '\n' arrives.
    auto w = make_backend();
    const std::vector<std::uint8_t> scout{0x20, 0x00, 0xFE, 0x00, 0x80, 0x99};
    const std::vector<std::uint8_t> data_frame{0x20, 0x00, 0xFE, 0x00, 0x42};
    const std::string line =
        "RX_TRANSMIT " + encode_base64(scout) + " " + encode_base64(data_frame) + "\n";

    // Chunk it into pieces of 3 bytes each (or whatever's left).
    constexpr std::size_t chunk_size = 3;
    for (std::size_t i = 0; i < line.size(); i += chunk_size) {
        w.mock->stage_read_chunk(line.substr(i, chunk_size));
    }

    auto frame = wait_for_frame(*w.backend);
    REQUIRE(frame.has_value());
    CHECK(frame->type == FrameType::Unicast);
    CHECK(frame->data == std::vector<std::uint8_t>{0x42});
}

TEST_CASE("PiconetBackend reader handles two events delivered in one read",
          "[piconet][backend][rx]") {
    auto w = make_backend();
    const std::vector<std::uint8_t> scout{0x20, 0x00, 0xFE, 0x00, 0x80, 0x99};
    const std::vector<std::uint8_t> data_a{0x20, 0x00, 0xFE, 0x00, 0xA1};
    const std::vector<std::uint8_t> data_b{0x20, 0x00, 0xFE, 0x00, 0xB2};
    const std::string both =
        "RX_TRANSMIT " + encode_base64(scout) + " " + encode_base64(data_a) + "\n" +
        "RX_TRANSMIT " + encode_base64(scout) + " " + encode_base64(data_b) + "\n";
    w.mock->stage_read_chunk(both);

    auto first = wait_for_frame(*w.backend);
    REQUIRE(first.has_value());
    CHECK(first->data == std::vector<std::uint8_t>{0xA1});

    auto second = wait_for_frame(*w.backend);
    REQUIRE(second.has_value());
    CHECK(second->data == std::vector<std::uint8_t>{0xB2});
}

TEST_CASE("PiconetBackend reader handles RX_BROADCAST", "[piconet][backend][rx]") {
    auto w = make_backend();
    const std::vector<std::uint8_t> wire{
        0xFF, 0xFF,  // broadcast addressing
        0xFE, 0x00,  // src
        0x9F, 0x80,  // ctrl, port
        'B', 'R', 'D', 'C',  // payload
    };
    w.mock->stage_read_chunk("RX_BROADCAST " + encode_base64(wire) + "\n");

    auto frame = wait_for_frame(*w.backend);
    REQUIRE(frame.has_value());
    CHECK(frame->type == FrameType::Broadcast);
    CHECK(frame->dest_stn == 0xFF);
    CHECK(frame->src_stn == 0xFE);
    CHECK(frame->control_byte == 0x1F);  // High bit masked.
    CHECK(frame->port == 0x80);
    CHECK(frame->data == std::vector<std::uint8_t>{'B', 'R', 'D', 'C'});
}

TEST_CASE("PiconetBackend reader handles RX_IMMEDIATE", "[piconet][backend][rx]") {
    auto w = make_backend();
    const std::vector<std::uint8_t> scout{
        0x20, 0x00,  // dest = us
        0xFE, 0x00,  // src
        0x82, 0x00,  // ctrl=Halt, port=0 (immediate)
    };
    const std::vector<std::uint8_t> data_frame{0x20, 0x00, 0xFE, 0x00};
    w.mock->stage_read_chunk(
        "RX_IMMEDIATE " + encode_base64(scout) + " " + encode_base64(data_frame) + "\n");

    auto frame = wait_for_frame(*w.backend);
    REQUIRE(frame.has_value());
    CHECK(frame->type == FrameType::Immediate);
    CHECK(frame->dest_stn == 32);
    CHECK(frame->src_stn == 254);
    CHECK(frame->control_byte == 0x02);  // Halt with high bit stripped.
    CHECK(frame->port == 0);
}

TEST_CASE("PiconetBackend reader emits a bare Ack on TX_RESULT OK",
          "[piconet][backend][rx]") {
    // FourWayHandshake's Stage::DataSent handler accepts any FrameType::Ack
    // to short-circuit the synthetic final-ack timer (it ignores the Ack's
    // addressing fields). Without this short-circuit, every successful TX
    // would wait the full 5ms FINAL_ACK_TIMEOUT or 250ms watchdog.
    auto w = make_backend();
    w.mock->stage_read_chunk("TX_RESULT OK\n");

    auto frame = wait_for_frame(*w.backend);
    REQUIRE(frame.has_value());
    CHECK(frame->type == FrameType::Ack);
}

TEST_CASE("PiconetBackend reader emits a Nack, not an Ack, on TX_RESULT failures",
          "[piconet][backend][rx]") {
    // A non-OK result used to be dropped, which left FourWayHandshake's timer
    // to synthesise a successful final ack -- so a frame that failed on the
    // wire was reported to the guest as delivered. Piconet cannot know a
    // station is absent before trying, since its firmware runs the wire
    // handshake and reports afterwards, so saying so afterwards is the only
    // honest option it has. See docs/discussion/aun-robustness.md defect 2.
    auto codes = GENERATE(
        "NO_SCOUT_ACK", "NO_DATA_ACK", "NOT_LISTENING",
        "LINE_JAMMED", "NO_CLOCK", "TIMEOUT", "OVERFLOW", "UNDERRUN");
    CAPTURE(codes);

    auto w = make_backend();
    w.mock->stage_read_chunk(std::string("TX_RESULT ") + codes + "\n");

    auto frame = wait_for_frame(*w.backend, std::chrono::milliseconds(250));
    REQUIRE(frame.has_value());
    CHECK(frame->type == FrameType::Nack);
}

TEST_CASE("PiconetBackend reader emits an Ack on TX_RESULT OK",
          "[piconet][backend][rx]") {
    auto w = make_backend();
    w.mock->stage_read_chunk("TX_RESULT OK\n");

    auto frame = wait_for_frame(*w.backend, std::chrono::milliseconds(250));
    REQUIRE(frame.has_value());
    CHECK(frame->type == FrameType::Ack);
}

TEST_CASE("PiconetBackend reader ignores STATUS / ERROR / unknown lines without crashing",
          "[piconet][backend][rx]") {
    auto w = make_backend();
    w.mock->stage_read_chunk(
        "STATUS 2.0.20 2 c3 1\n"
        "ERROR something wrong\n"
        "UNKNOWN_FUTURE_EVENT\n");

    // None of those should produce a NetworkFrame.
    auto frame = wait_for_frame(*w.backend, std::chrono::milliseconds(150));
    CHECK_FALSE(frame.has_value());

    // Real RX after the noise still works.
    const std::vector<std::uint8_t> scout{0x20, 0x00, 0xFE, 0x00, 0x80, 0x99};
    const std::vector<std::uint8_t> data_frame{0x20, 0x00, 0xFE, 0x00, 0x55};
    w.mock->stage_read_chunk(
        "RX_TRANSMIT " + encode_base64(scout) + " " + encode_base64(data_frame) + "\n");
    auto recovered = wait_for_frame(*w.backend);
    REQUIRE(recovered.has_value());
    CHECK(recovered->type == FrameType::Unicast);
}

TEST_CASE("PiconetBackend reader tolerates a trailing CR before the LF",
          "[piconet][backend][rx]") {
    // The firmware emits just '\n', but some serial setups translate
    // the line terminator into '\r\n'. Either should parse cleanly.
    auto w = make_backend();
    w.mock->stage_read_chunk("TX_RESULT OK\r\n");

    auto frame = wait_for_frame(*w.backend);
    REQUIRE(frame.has_value());
    CHECK(frame->type == FrameType::Ack);
}
