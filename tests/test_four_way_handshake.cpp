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

#include <beebium/econet/EconetSocket.hpp>
#include <beebium/econet/FourWayHandshake.hpp>
#include <beebium/econet/TestBackend.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace beebium;

namespace {

// Helper to create a RawFrame from byte data (as the ADLC would send).
NetworkFrame make_raw_frame(std::vector<uint8_t> data) {
    NetworkFrame frame;
    frame.type = FrameType::RawFrame;
    frame.data = std::move(data);
    return frame;
}

// Helper to tick the handshake N times.
void tick_n(FourWayHandshake& hs, int n) {
    for (int i = 0; i < n; ++i) {
        hs.tick();
    }
}

// Econet frame byte offsets
constexpr int DEST_STN = 0;
constexpr int DEST_NET = 1;
constexpr int SRC_STN  = 2;
constexpr int SRC_NET  = 3;
constexpr int CTRL     = 4;
constexpr int PORT     = 5;

}  // namespace

// =============================================================================
// Initial State
// =============================================================================

TEST_CASE("FourWayHandshake: initial state is Idle", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);
    CHECK_FALSE(hs.flag_fill_active());
}

TEST_CASE("FourWayHandshake: receive_frame returns nullopt when idle and no packets", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    CHECK_FALSE(hs.receive_frame().has_value());
}

TEST_CASE("FourWayHandshake: is_connected delegates to backend", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    CHECK(hs.is_connected());
    backend.set_connected(false);
    CHECK_FALSE(hs.is_connected());
}

TEST_CASE("FourWayHandshake: is_receiving_flags true when idle and connected (clock box)", "[econet][handshake]") {
    TestBackend backend;
    backend.set_connected(true);
    FourWayHandshake hs(backend);

    // Idle + connected = clock box flag fill active
    CHECK(hs.is_receiving_flags());
}

TEST_CASE("FourWayHandshake: is_receiving_flags false when idle and disconnected", "[econet][handshake]") {
    TestBackend backend;
    backend.set_connected(false);
    FourWayHandshake hs(backend);

    // Idle + disconnected = no clock box, no flag fill
    CHECK_FALSE(hs.is_receiving_flags());
}

// =============================================================================
// Scout Payload Sizes
// =============================================================================

TEST_CASE("FourWayHandshake: scout_payload_size for POKE (0x02) is 8", "[econet][handshake]") {
    CHECK(FourWayHandshake::scout_payload_size(0x02) == 8);
    CHECK(FourWayHandshake::scout_payload_size(0x82) == 8);  // With bit 7
}

TEST_CASE("FourWayHandshake: scout_payload_size for JSR/UserProc/OSProc is 4", "[econet][handshake]") {
    CHECK(FourWayHandshake::scout_payload_size(0x03) == 4);
    CHECK(FourWayHandshake::scout_payload_size(0x04) == 4);
    CHECK(FourWayHandshake::scout_payload_size(0x05) == 4);
    CHECK(FourWayHandshake::scout_payload_size(0x83) == 4);  // With bit 7
}

TEST_CASE("FourWayHandshake: scout_payload_size for other control bytes is 0", "[econet][handshake]") {
    CHECK(FourWayHandshake::scout_payload_size(0x00) == 0);
    CHECK(FourWayHandshake::scout_payload_size(0x01) == 0);
    CHECK(FourWayHandshake::scout_payload_size(0x80) == 0);  // 0x00 with bit 7
    CHECK(FourWayHandshake::scout_payload_size(0x06) == 0);
    CHECK(FourWayHandshake::scout_payload_size(0xFF) == 0);
}

// =============================================================================
// TX: Unicast — Full Four-Way Handshake (Scout → ScoutAck → Data → Ack)
// =============================================================================

TEST_CASE("FourWayHandshake: TX unicast scout is held, not sent to backend", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Beeb sends a scout: dest=254.0, src=1.0, ctrl=0x80, port=0x99
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x80, 0x99}));

    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutSent);
    CHECK(backend.sent_frame_count() == 0);  // Scout NOT sent to network
    CHECK(hs.flag_fill_active());
}

TEST_CASE("FourWayHandshake: TX unicast fake scout ack delivered after timeout", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Beeb sends scout
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x80, 0x99}));
    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutSent);

    // Tick until scout ack timeout fires
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);

    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutAckReceived);

    // ADLC should now be able to receive the fake scout ack
    auto frame = hs.receive_frame();
    REQUIRE(frame.has_value());
    CHECK(frame->type == FrameType::RawFrame);
    // Scout ack is a 4-byte frame: src/dest swapped from scout
    REQUIRE(frame->data.size() == 4);
    CHECK(frame->data[DEST_STN] == 1);    // Ack TO us (src of scout)
    CHECK(frame->data[DEST_NET] == 0);
    CHECK(frame->data[SRC_STN] == 254);   // Ack FROM them (dest of scout)
    CHECK(frame->data[SRC_NET] == 0);
}

TEST_CASE("FourWayHandshake: TX unicast data sent as AUN Unicast after scout ack", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Scout
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x80, 0x99}));
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    hs.receive_frame();  // Consume fake scout ack

    // Beeb sends data frame (4 address bytes + payload)
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0xAA, 0xBB, 0xCC}));

    CHECK(hs.stage() == FourWayHandshake::Stage::DataSent);
    REQUIRE(backend.sent_frame_count() == 1);

    const auto& sent = backend.sent_network_frames()[0];
    CHECK(sent.type == FrameType::Unicast);
    CHECK(sent.dest_stn == 254);
    CHECK(sent.dest_net == 0);
    CHECK(sent.src_stn == 1);
    CHECK(sent.src_net == 0);
    CHECK(sent.port == 0x99);
    CHECK(sent.control_byte == 0x00);  // 0x80 & 0x7F = 0x00

    // Payload: data frame bytes after the 4 address bytes
    REQUIRE(sent.data.size() == 3);
    CHECK(sent.data[0] == 0xAA);
    CHECK(sent.data[1] == 0xBB);
    CHECK(sent.data[2] == 0xCC);
}

TEST_CASE("FourWayHandshake: TX unicast remote ack generates fake final ack", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Scout → timeout → data
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x80, 0x99}));
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    hs.receive_frame();
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0xAA}));

    CHECK(hs.stage() == FourWayHandshake::Stage::DataSent);

    // Remote sends AUN Ack
    NetworkFrame ack;
    ack.type = FrameType::Ack;
    ack.dest_stn = 1;
    ack.dest_net = 0;
    ack.src_stn = 254;
    ack.src_net = 0;
    backend.inject_rx_network_frame(ack);

    // Handshake should deliver a fake final ack to the ADLC
    auto frame = hs.receive_frame();
    REQUIRE(frame.has_value());
    CHECK(frame->type == FrameType::RawFrame);
    REQUIRE(frame->data.size() == 4);
    CHECK(frame->data[DEST_STN] == 1);    // Final ack TO us
    CHECK(frame->data[SRC_STN] == 254);   // Final ack FROM them

    // Should transition to WaitForIdle, then Idle once queue empty
    hs.tick();
    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);
    CHECK_FALSE(hs.flag_fill_active());
}

// =============================================================================
// TX: Broadcast — Fire-and-Forget
// =============================================================================

TEST_CASE("FourWayHandshake: TX broadcast sent immediately to backend", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Beeb sends broadcast: dest station 0xFF
    hs.send_frame(make_raw_frame({0xFF, 0, 1, 0, 0x80, 0x99, 0x42}));

    REQUIRE(backend.sent_frame_count() == 1);
    const auto& sent = backend.sent_network_frames()[0];
    CHECK(sent.type == FrameType::Broadcast);
    CHECK(sent.dest_stn == 0xFF);
    CHECK(sent.src_stn == 1);
    CHECK(sent.port == 0x99);
    CHECK(sent.control_byte == 0x00);  // 0x80 & 0x7F

    REQUIRE(sent.data.size() == 1);
    CHECK(sent.data[0] == 0x42);

    // Transitions through WaitForIdle
    CHECK(hs.flag_fill_active());
    hs.tick();
    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);
}

// =============================================================================
// TX: Immediate Operations
// =============================================================================

TEST_CASE("FourWayHandshake: TX immediate sent to backend, waits for reply", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Beeb sends immediate: port 0x00
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x88, 0x00, 0xDD}));

    CHECK(hs.stage() == FourWayHandshake::Stage::ImmediateSent);
    REQUIRE(backend.sent_frame_count() == 1);

    const auto& sent = backend.sent_network_frames()[0];
    CHECK(sent.type == FrameType::Immediate);
    CHECK(sent.port == 0x00);
    CHECK(sent.control_byte == 0x08);  // 0x88 & 0x7F
    REQUIRE(sent.data.size() == 1);
    CHECK(sent.data[0] == 0xDD);
}

TEST_CASE("FourWayHandshake: TX immediate reply received and delivered", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Send immediate
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x88, 0x00}));

    // Remote replies
    NetworkFrame reply;
    reply.type = FrameType::ImmReply;
    reply.dest_stn = 1;
    reply.dest_net = 0;
    reply.src_stn = 254;
    reply.src_net = 0;
    reply.data = {0xEE, 0xFF};
    backend.inject_rx_network_frame(reply);

    auto frame = hs.receive_frame();
    REQUIRE(frame.has_value());
    CHECK(frame->type == FrameType::RawFrame);
    // 4 address bytes + reply payload
    REQUIRE(frame->data.size() == 6);
    CHECK(frame->data[DEST_STN] == 1);    // TO us
    CHECK(frame->data[SRC_STN] == 254);   // FROM them
    CHECK(frame->data[4] == 0xEE);
    CHECK(frame->data[5] == 0xFF);

    hs.tick();
    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);
}

// =============================================================================
// RX: Unicast — Incoming AUN Data → Four-Way Handshake for Beeb
// =============================================================================

TEST_CASE("FourWayHandshake: RX unicast delivers scout frame to Beeb", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Remote sends AUN Unicast to us
    NetworkFrame pkt;
    pkt.type = FrameType::Unicast;
    pkt.dest_stn = 1;
    pkt.dest_net = 0;
    pkt.src_stn = 254;
    pkt.src_net = 0;
    pkt.control_byte = 0x00;  // AUN ctrl (bit 7 clear)
    pkt.port = 0x99;
    pkt.data = {0xAA, 0xBB};  // Payload
    backend.inject_rx_network_frame(pkt);

    auto frame = hs.receive_frame();
    REQUIRE(frame.has_value());
    CHECK(frame->type == FrameType::RawFrame);
    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutReceived);
    CHECK(hs.flag_fill_active());

    // Scout frame: dest, dest_net, src, src_net, ctrl|0x80, port
    REQUIRE(frame->data.size() >= 6);
    CHECK(frame->data[DEST_STN] == 1);
    CHECK(frame->data[DEST_NET] == 0);
    CHECK(frame->data[SRC_STN] == 254);
    CHECK(frame->data[SRC_NET] == 0);
    CHECK(frame->data[CTRL] == 0x80);  // 0x00 | 0x80
    CHECK(frame->data[PORT] == 0x99);
}

TEST_CASE("FourWayHandshake: RX unicast Beeb sends scout ack, data delivered after timeout", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Inject unicast
    NetworkFrame pkt;
    pkt.type = FrameType::Unicast;
    pkt.dest_stn = 1;
    pkt.dest_net = 0;
    pkt.src_stn = 254;
    pkt.src_net = 0;
    pkt.control_byte = 0x00;
    pkt.port = 0x99;
    pkt.data = {0xAA, 0xBB};
    backend.inject_rx_network_frame(pkt);

    // Consume scout
    hs.receive_frame();
    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutReceived);

    // Beeb sends scout ack (4 address bytes)
    hs.send_frame(make_raw_frame({254, 0, 1, 0}));
    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutAckSent);

    // Nothing sent to network (scout ack is local)
    CHECK(backend.sent_frame_count() == 0);

    // Tick until timeout delivers cached data
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    CHECK(hs.stage() == FourWayHandshake::Stage::DataReceived);

    auto frame = hs.receive_frame();
    REQUIRE(frame.has_value());
    CHECK(frame->type == FrameType::RawFrame);
    // Data frame: 4 address bytes + payload
    REQUIRE(frame->data.size() == 6);
    CHECK(frame->data[DEST_STN] == 1);    // dest = us
    CHECK(frame->data[SRC_STN] == 254);   // src = them
    CHECK(frame->data[4] == 0xAA);
    CHECK(frame->data[5] == 0xBB);
}

TEST_CASE("FourWayHandshake: RX unicast Beeb sends final ack, AUN Ack sent to network", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Full RX flow: unicast → scout → scout ack → data → final ack
    NetworkFrame pkt;
    pkt.type = FrameType::Unicast;
    pkt.dest_stn = 1;
    pkt.dest_net = 0;
    pkt.src_stn = 254;
    pkt.src_net = 0;
    pkt.control_byte = 0x00;
    pkt.port = 0x99;
    pkt.data = {0xAA};
    backend.inject_rx_network_frame(pkt);

    hs.receive_frame();  // Scout
    hs.send_frame(make_raw_frame({254, 0, 1, 0}));  // Scout ack
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    hs.receive_frame();  // Data

    CHECK(hs.stage() == FourWayHandshake::Stage::DataReceived);

    // Beeb sends final ack
    hs.send_frame(make_raw_frame({254, 0, 1, 0}));

    // AUN Ack should be sent to network
    REQUIRE(backend.sent_frame_count() == 1);
    const auto& sent = backend.sent_network_frames()[0];
    CHECK(sent.type == FrameType::Ack);
    CHECK(sent.dest_stn == 254);  // Ack TO the original source
    CHECK(sent.src_stn == 1);     // Ack FROM us

    CHECK_FALSE(hs.flag_fill_active());
    hs.tick();
    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);
}

// =============================================================================
// RX: Broadcast — Delivered Directly
// =============================================================================

TEST_CASE("FourWayHandshake: RX broadcast delivered directly, stays in Idle", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    NetworkFrame pkt;
    pkt.type = FrameType::Broadcast;
    pkt.dest_stn = 0xFF;
    pkt.dest_net = 0;
    pkt.src_stn = 254;
    pkt.src_net = 0;
    pkt.control_byte = 0x00;
    pkt.port = 0x99;
    pkt.data = {0x42};
    backend.inject_rx_network_frame(pkt);

    auto frame = hs.receive_frame();
    REQUIRE(frame.has_value());
    CHECK(frame->type == FrameType::RawFrame);

    // Econet frame: dest, dest_net, src, src_net, ctrl|0x80, port, data
    REQUIRE(frame->data.size() == 7);
    CHECK(frame->data[DEST_STN] == 0xFF);
    CHECK(frame->data[SRC_STN] == 254);
    CHECK(frame->data[CTRL] == 0x80);
    CHECK(frame->data[PORT] == 0x99);
    CHECK(frame->data[6] == 0x42);

    // Stays in Idle — broadcasts are fire-and-forget
    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);
}

// =============================================================================
// RX: Immediate Operations
// =============================================================================

TEST_CASE("FourWayHandshake: RX immediate delivered, Beeb reply sent as ImmReply", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Remote sends immediate request
    NetworkFrame pkt;
    pkt.type = FrameType::Immediate;
    pkt.dest_stn = 1;
    pkt.dest_net = 0;
    pkt.src_stn = 254;
    pkt.src_net = 0;
    pkt.control_byte = 0x08;
    pkt.port = 0x00;
    pkt.data = {};
    backend.inject_rx_network_frame(pkt);

    auto frame = hs.receive_frame();
    REQUIRE(frame.has_value());
    CHECK(hs.stage() == FourWayHandshake::Stage::ImmediateReceived);

    // Econet frame delivered to Beeb
    REQUIRE(frame->data.size() >= 6);
    CHECK(frame->data[DEST_STN] == 1);
    CHECK(frame->data[SRC_STN] == 254);
    CHECK(frame->data[CTRL] == 0x88);  // 0x08 | 0x80
    CHECK(frame->data[PORT] == 0x00);

    // Beeb sends reply
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0xEE, 0xFF}));

    REQUIRE(backend.sent_frame_count() == 1);
    const auto& sent = backend.sent_network_frames()[0];
    CHECK(sent.type == FrameType::ImmReply);
    CHECK(sent.dest_stn == 254);  // Reply TO requester
    CHECK(sent.src_stn == 1);     // Reply FROM us
    REQUIRE(sent.data.size() == 2);
    CHECK(sent.data[0] == 0xEE);
    CHECK(sent.data[1] == 0xFF);

    hs.tick();
    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);
}

// =============================================================================
// TX: Port-0 Classification — Unicast (POKE/JSR/UserProc/OSProc) vs Immediate
// =============================================================================

TEST_CASE("FourWayHandshake: TX port-0 with ctrl 0x82 (POKE) is Unicast scout, not Immediate", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Port 0 + ctrl 0x82 = POKE = Unicast scout with 8 extra bytes
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x82, 0x00,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}));

    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutSent);
    CHECK(backend.sent_frame_count() == 0);  // Scout NOT sent (held for four-way)
}

TEST_CASE("FourWayHandshake: TX port-0 with ctrl 0x83 (JSR) is Unicast scout, not Immediate", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Port 0 + ctrl 0x83 = JSR = Unicast scout with 4 extra bytes
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x83, 0x00,
        0x01, 0x02, 0x03, 0x04}));

    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutSent);
    CHECK(backend.sent_frame_count() == 0);
}

TEST_CASE("FourWayHandshake: TX port-0 with ctrl 0x88 (Machine Peek) is Immediate", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Port 0 + ctrl 0x88 = Machine Peek = Immediate
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x88, 0x00}));

    CHECK(hs.stage() == FourWayHandshake::Stage::ImmediateSent);
    REQUIRE(backend.sent_frame_count() == 1);
    CHECK(backend.sent_network_frames()[0].type == FrameType::Immediate);
}

// =============================================================================
// TX: Scout Payload Sizes — Extra Bytes in AUN Unicast
// =============================================================================

TEST_CASE("FourWayHandshake: TX unicast with POKE (ctrl 0x82) includes 8 scout extra bytes", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Scout with ctrl=0x82 and 8 extra bytes
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x82, 0x99,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}));

    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    hs.receive_frame();

    // Data frame (4 address bytes + data payload)
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0xDD, 0xEE}));

    REQUIRE(backend.sent_frame_count() == 1);
    const auto& sent = backend.sent_network_frames()[0];
    CHECK(sent.type == FrameType::Unicast);
    CHECK(sent.control_byte == 0x02);  // 0x82 & 0x7F

    // AUN payload = 8 scout extra bytes + 2 data payload bytes
    REQUIRE(sent.data.size() == 10);
    CHECK(sent.data[0] == 0x01);
    CHECK(sent.data[7] == 0x08);
    CHECK(sent.data[8] == 0xDD);
    CHECK(sent.data[9] == 0xEE);
}

TEST_CASE("FourWayHandshake: TX unicast with JSR (ctrl 0x83) includes 4 scout extra bytes", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Scout with ctrl=0x83 and 4 extra bytes
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x83, 0x99,
        0x01, 0x02, 0x03, 0x04}));

    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    hs.receive_frame();

    // Data frame
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0xDD}));

    REQUIRE(backend.sent_frame_count() == 1);
    const auto& sent = backend.sent_network_frames()[0];

    // AUN payload = 4 scout extra bytes + 1 data payload byte
    REQUIRE(sent.data.size() == 5);
    CHECK(sent.data[0] == 0x01);
    CHECK(sent.data[3] == 0x04);
    CHECK(sent.data[4] == 0xDD);
}

TEST_CASE("FourWayHandshake: TX unicast with normal ctrl (0x80) has no scout extra bytes", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Scout with ctrl=0x80, no extra bytes
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x80, 0x99}));

    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    hs.receive_frame();

    // Data frame
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0xDD, 0xEE}));

    REQUIRE(backend.sent_frame_count() == 1);
    const auto& sent = backend.sent_network_frames()[0];

    // AUN payload = only data payload bytes (no scout extras)
    REQUIRE(sent.data.size() == 2);
    CHECK(sent.data[0] == 0xDD);
    CHECK(sent.data[1] == 0xEE);
}

// =============================================================================
// RX: Scout Payload Sizes — Extra Bytes in Incoming Scout
// =============================================================================

TEST_CASE("FourWayHandshake: RX unicast with POKE splits scout extra from data", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // AUN Unicast with ctrl=0x02 (POKE, 8 extra bytes) and total 10 data bytes
    NetworkFrame pkt;
    pkt.type = FrameType::Unicast;
    pkt.dest_stn = 1;
    pkt.dest_net = 0;
    pkt.src_stn = 254;
    pkt.src_net = 0;
    pkt.control_byte = 0x02;
    pkt.port = 0x99;
    pkt.data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0xAA, 0xBB};
    backend.inject_rx_network_frame(pkt);

    // Scout should include the 8 extra bytes
    auto scout = hs.receive_frame();
    REQUIRE(scout.has_value());
    // Scout: 4 addr + ctrl + port + 8 extra = 14 bytes
    REQUIRE(scout->data.size() == 14);
    CHECK(scout->data[CTRL] == 0x82);  // 0x02 | 0x80
    CHECK(scout->data[6] == 0x01);
    CHECK(scout->data[13] == 0x08);

    // Scout ack
    hs.send_frame(make_raw_frame({254, 0, 1, 0}));
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);

    // Data should contain the remaining 2 bytes
    auto data = hs.receive_frame();
    REQUIRE(data.has_value());
    // Data: 4 addr + 2 data bytes = 6 bytes
    REQUIRE(data->data.size() == 6);
    CHECK(data->data[4] == 0xAA);
    CHECK(data->data[5] == 0xBB);
}

// =============================================================================
// Timeouts
// =============================================================================

TEST_CASE("FourWayHandshake: scout ack timeout fires at exactly SCOUT_ACK_TIMEOUT ticks", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x80, 0x99}));
    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutSent);

    // One tick before timeout
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT - 1);
    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutSent);

    // Exactly at timeout
    hs.tick();
    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutAckReceived);
}

TEST_CASE("FourWayHandshake: watchdog timeout resets to Idle", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Start a TX unicast but never complete it
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x80, 0x99}));
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    hs.receive_frame();
    // Now in ScoutAckReceived — don't send data, let watchdog fire

    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutAckReceived);

    // Watchdog was armed when scout was sent. Remaining time = WATCHDOG - SCOUT_ACK.
    int remaining = FourWayHandshake::WATCHDOG_TIMEOUT - FourWayHandshake::SCOUT_ACK_TIMEOUT;
    tick_n(hs, remaining);

    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);
    CHECK_FALSE(hs.flag_fill_active());
}

TEST_CASE("FourWayHandshake: fake final ack delivered after FINAL_ACK_TIMEOUT", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Full TX path: scout → scout ack timeout → data → DataSent
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x80, 0x99}));
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    hs.receive_frame();  // Consume fake scout ack
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0xAA}));

    CHECK(hs.stage() == FourWayHandshake::Stage::DataSent);

    // One tick before the final ack timer fires
    tick_n(hs, FourWayHandshake::FINAL_ACK_TIMEOUT - 1);
    CHECK(hs.stage() == FourWayHandshake::Stage::DataSent);

    // Exactly at FINAL_ACK_TIMEOUT
    hs.tick();
    CHECK(hs.stage() == FourWayHandshake::Stage::WaitForIdle);

    auto frame = hs.receive_frame();
    REQUIRE(frame.has_value());
    CHECK(frame->type == FrameType::RawFrame);
    REQUIRE(frame->data.size() == 4);
    CHECK(frame->data[DEST_STN] == 1);    // Final ack TO us (we were src)
    CHECK(frame->data[DEST_NET] == 0);
    CHECK(frame->data[SRC_STN] == 254);   // Final ack FROM them (they were dest)
    CHECK(frame->data[SRC_NET] == 0);
}

TEST_CASE("FourWayHandshake: real AUN Ack before timer cancels fake final ack", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // When a real server responds with AUN Ack before the FINAL_ACK_TIMEOUT,
    // handle_incoming() delivers the fake final ack and transitions to
    // WaitForIdle. The handshake timer becomes irrelevant.
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x80, 0x99}));
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    hs.receive_frame();
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0xDD}));
    CHECK(hs.stage() == FourWayHandshake::Stage::DataSent);

    // Remote server sends AUN Ack (well before FINAL_ACK_TIMEOUT)
    tick_n(hs, 100);
    NetworkFrame ack;
    ack.type = FrameType::Ack;
    ack.dest_stn = 1;
    ack.dest_net = 0;
    ack.src_stn = 254;
    ack.src_net = 0;
    backend.inject_rx_network_frame(ack);

    auto frame = hs.receive_frame();
    REQUIRE(frame.has_value());
    CHECK(frame->type == FrameType::RawFrame);
    REQUIRE(frame->data.size() == 4);
    CHECK(frame->data[DEST_STN] == 1);    // Final ack TO us
    CHECK(frame->data[SRC_STN] == 254);   // Final ack FROM them

    CHECK(hs.stage() == FourWayHandshake::Stage::WaitForIdle);

    // Drain the queue and tick to reach Idle
    hs.tick();
    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);
}

TEST_CASE("FourWayHandshake: watchdog during non-DataSent does not generate fake ack", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Get into ScoutAckReceived (not DataSent) and let watchdog fire
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x80, 0x99}));
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    hs.receive_frame();
    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutAckReceived);

    int remaining = FourWayHandshake::WATCHDOG_TIMEOUT - FourWayHandshake::SCOUT_ACK_TIMEOUT;
    tick_n(hs, remaining);

    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);

    // No fake ack should be enqueued for non-DataSent stages
    auto frame = hs.receive_frame();
    CHECK_FALSE(frame.has_value());
}

TEST_CASE("FourWayHandshake: flag fill times out after FLAG_FILL_TIMEOUT", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Start a broadcast to activate flag fill
    hs.send_frame(make_raw_frame({0xFF, 0, 1, 0, 0x80, 0x99}));
    CHECK(hs.flag_fill_active());

    // Tick one less than timeout
    tick_n(hs, FourWayHandshake::FLAG_FILL_TIMEOUT - 1);
    CHECK(hs.flag_fill_active());

    // At timeout
    hs.tick();
    CHECK_FALSE(hs.flag_fill_active());
}

// =============================================================================
// Reset
// =============================================================================

TEST_CASE("FourWayHandshake: reset clears all state", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Get into a non-idle state
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x80, 0x99}));
    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutSent);
    CHECK(hs.flag_fill_active());

    hs.reset();
    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);
    CHECK_FALSE(hs.flag_fill_active());
    CHECK_FALSE(hs.receive_frame().has_value());
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("FourWayHandshake: malformed frame (too short) is ignored", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Frame with < 4 bytes
    hs.send_frame(make_raw_frame({254, 0, 1}));

    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);
    CHECK(backend.sent_frame_count() == 0);
}

TEST_CASE("FourWayHandshake: unexpected TX in wrong state resets handshake", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Get into ScoutSent (TX path waiting for timeout)
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x80, 0x99}));
    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutSent);

    // Send another frame — unexpected in ScoutSent stage
    hs.send_frame(make_raw_frame({253, 0, 1, 0, 0x80, 0x88}));

    // Should reset to Idle
    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);
}

TEST_CASE("FourWayHandshake: unexpected AUN packet type in DataSent is held, not dropped",
          "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Full TX path to DataSent
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x80, 0x99}));
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    hs.receive_frame();
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0xDD}));
    CHECK(hs.stage() == FourWayHandshake::Stage::DataSent);

    // A Unicast arrives where only an Ack can be used. It must not reach the
    // ADLC now -- the guest is mid-transaction -- but it must not be thrown
    // away either: it has already been taken off the socket, so discarding it
    // loses it for good. This test previously asserted the discard.
    NetworkFrame wrong;
    wrong.type = FrameType::Unicast;
    wrong.data = {0x42};
    backend.inject_rx_network_frame(wrong);

    auto frame = hs.receive_frame();
    CHECK_FALSE(frame.has_value());
    CHECK(hs.stage() == FourWayHandshake::Stage::DataSent);  // Unchanged
    CHECK(hs.held_frame_count() == 1);                       // ... and retained
}

TEST_CASE("FourWayHandshake: WaitForIdle transitions to Idle when queue drains", "[econet][handshake]") {
    TestBackend backend;
    FourWayHandshake hs(backend);

    // Broadcast puts us in WaitForIdle with flag fill
    hs.send_frame(make_raw_frame({0xFF, 0, 1, 0, 0x80, 0x99}));

    // Before tick, stage is WaitForIdle
    // After tick, queue is empty → transitions to Idle
    hs.tick();
    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);
}

// =============================================================================
// EconetSocket AUN Mode Integration
// =============================================================================

TEST_CASE("EconetSocket: enable with aun_mode creates FourWayHandshake", "[econet][socket][handshake]") {
    EconetSocket socket;
    auto backend = std::make_unique<TestBackend>();
    socket.enable(1, std::move(backend), true);

    CHECK(socket.enabled());
    CHECK(socket.adlc() != nullptr);
    CHECK(socket.handshake() != nullptr);
    CHECK(socket.handshake()->stage() == FourWayHandshake::Stage::Idle);
}

TEST_CASE("EconetSocket: enable without aun_mode has no handshake", "[econet][socket][handshake]") {
    EconetSocket socket;
    auto backend = std::make_unique<TestBackend>();
    socket.enable(1, std::move(backend), false);

    CHECK(socket.enabled());
    CHECK(socket.adlc() != nullptr);
    CHECK(socket.handshake() == nullptr);
}

TEST_CASE("EconetSocket: enable with default aun_mode has no handshake", "[econet][socket][handshake]") {
    EconetSocket socket;
    auto backend = std::make_unique<TestBackend>();
    socket.enable(1, std::move(backend));

    CHECK(socket.handshake() == nullptr);
}

TEST_CASE("EconetSocket: disable clears handshake", "[econet][socket][handshake]") {
    EconetSocket socket;
    auto backend = std::make_unique<TestBackend>();
    socket.enable(1, std::move(backend), true);
    CHECK(socket.handshake() != nullptr);

    socket.disable();
    CHECK(socket.handshake() == nullptr);
}

TEST_CASE("EconetSocket: reset resets handshake", "[econet][socket][handshake]") {
    EconetSocket socket;
    auto backend = std::make_unique<TestBackend>();
    auto* raw_backend = backend.get();
    socket.enable(1, std::move(backend), true);

    // Put handshake into non-idle state by injecting a unicast
    NetworkFrame pkt;
    pkt.type = FrameType::Unicast;
    pkt.dest_stn = 1;
    pkt.dest_net = 0;
    pkt.src_stn = 254;
    pkt.src_net = 0;
    pkt.control_byte = 0x00;
    pkt.port = 0x99;
    pkt.data = {0xAA};
    raw_backend->inject_rx_network_frame(pkt);

    // Tick to let handshake process the packet
    socket.tick_rising();

    socket.reset();
    CHECK(socket.handshake()->stage() == FourWayHandshake::Stage::Idle);
}


// =============================================================================
// RX then TX: Server reply scenario (L3FS)
// =============================================================================

TEST_CASE("FourWayHandshake: TX scout succeeds immediately after RX handshake completes",
          "[econet][handshake][rx-then-tx]") {
    // This models the L3FS server scenario:
    // 1. Receive incoming request (full RX four-way handshake)
    // 2. Process request (just tick some time)
    // 3. Send reply (TX four-way handshake)
    // The TX must complete — this is the exact path that fails in Beebium-to-Beebium.

    TestBackend backend;
    FourWayHandshake hs(backend);

    // === Phase 1: Receive incoming request (client *I AM) ===
    NetworkFrame incoming;
    incoming.type = FrameType::Unicast;
    incoming.dest_stn = 254;
    incoming.dest_net = 0;
    incoming.src_stn = 221;
    incoming.src_net = 0;
    incoming.control_byte = 0x00;
    incoming.port = 0x99;
    incoming.data = {0x90, 0x01, 0x02};  // Reply port &90 + *I AM data
    backend.inject_rx_network_frame(incoming);

    // Scout delivery
    auto scout = hs.receive_frame();
    REQUIRE(scout.has_value());
    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutReceived);

    // Beeb sends scout ack
    hs.send_frame(make_raw_frame({221, 0, 254, 0}));
    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutAckSent);

    // Timer delivers data
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    auto data = hs.receive_frame();
    REQUIRE(data.has_value());
    CHECK(hs.stage() == FourWayHandshake::Stage::DataReceived);

    // Beeb sends final ack
    hs.send_frame(make_raw_frame({221, 0, 254, 0}));
    CHECK_FALSE(hs.flag_fill_active());

    // Transition to Idle
    hs.tick();
    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);

    // AUN Ack should have been sent to network
    REQUIRE(backend.sent_frame_count() == 1);
    CHECK(backend.sent_network_frames()[0].type == FrameType::Ack);

    // === Phase 2: Simulate processing time (like L3FS disc I/O) ===
    // Tick past the idle cooldown
    tick_n(hs, FourWayHandshake::IDLE_COOLDOWN + 100);
    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);

    // Clear sent frames for clean counting
    backend.clear_sent_frames();

    // === Phase 3: Send reply (server TX, port &90) ===
    // Scout from server (station 254) to client (station 221)
    hs.send_frame(make_raw_frame({221, 0, 254, 0, 0x80, 0x90}));
    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutSent);

    // Scout should NOT be sent to backend (held for four-way)
    CHECK(backend.sent_frame_count() == 0);

    // Timer fires — fake scout ack
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    auto scout_ack = hs.receive_frame();
    REQUIRE(scout_ack.has_value());
    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutAckReceived);

    // Beeb sends data frame (reply data)
    hs.send_frame(make_raw_frame({221, 0, 254, 0, 0xDD, 0xEE}));
    CHECK(hs.stage() == FourWayHandshake::Stage::DataSent);

    // AUN Unicast should have been sent
    REQUIRE(backend.sent_frame_count() == 1);
    const auto& reply = backend.sent_network_frames()[0];
    CHECK(reply.type == FrameType::Unicast);
    CHECK(reply.dest_stn == 221);
    CHECK(reply.src_stn == 254);
    CHECK(reply.port == 0x90);

    // Simulate remote AUN Ack (client acknowledges)
    NetworkFrame ack;
    ack.type = FrameType::Ack;
    ack.dest_stn = 254;
    ack.dest_net = 0;
    ack.src_stn = 221;
    ack.src_net = 0;
    backend.inject_rx_network_frame(ack);

    tick_n(hs, 100);
    auto final_ack = hs.receive_frame();
    REQUIRE(final_ack.has_value());

    // Should complete
    hs.tick();
    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);
}


TEST_CASE("FourWayHandshake: stale watchdog from RX does not kill subsequent TX scout ack timer",
          "[econet][handshake][rx-then-tx][watchdog]") {
    // This reproduces the L3FS server bug: the incoming request's watchdog
    // timer survives the RX handshake completion and fires during the
    // server's reply TX, resetting the scout ack timer.
    //
    // The key timing: the watchdog is 500,000 ticks. If the reply TX
    // starts before the watchdog fires, the scout ack timer (5000 ticks)
    // is killed when the watchdog fires at tick 500,000.

    TestBackend backend;
    FourWayHandshake hs(backend);

    // === Phase 1: Complete RX handshake (arms watchdog at 500,000) ===
    NetworkFrame incoming;
    incoming.type = FrameType::Unicast;
    incoming.dest_stn = 254;
    incoming.dest_net = 0;
    incoming.src_stn = 221;
    incoming.src_net = 0;
    incoming.control_byte = 0x00;
    incoming.port = 0x99;
    incoming.data = {0x90, 0x01};
    backend.inject_rx_network_frame(incoming);

    hs.receive_frame();  // Scout
    hs.send_frame(make_raw_frame({221, 0, 254, 0}));  // Scout ack
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    hs.receive_frame();  // Data
    hs.send_frame(make_raw_frame({221, 0, 254, 0}));  // Final ack

    hs.tick();
    REQUIRE(hs.stage() == FourWayHandshake::Stage::Idle);

    // === Phase 2: Simulate L3FS processing time ===
    // Tick LESS than the watchdog timeout. On the buggy code, the
    // watchdog from Phase 1 is still counting down.
    // 200,000 ticks = 100ms, well within the 500,000 tick watchdog.
    tick_n(hs, 200'000);
    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);

    backend.clear_sent_frames();

    // === Phase 3: Start reply TX ===
    hs.send_frame(make_raw_frame({221, 0, 254, 0, 0x80, 0x90}));
    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutSent);

    // === Phase 4: Wait for scout ack timer (5000 ticks) ===
    // On the buggy code, the stale watchdog fires at tick ~300,000
    // (500,000 - 200,000 already elapsed), which is BEFORE the
    // scout ack timer fires at 5000 ticks from Phase 3.
    //
    // Wait: 300,000 > 5000, so the scout ack timer SHOULD fire first.
    // But if Phase 2 ticked 495,000 instead of 200,000, the watchdog
    // would fire after only 5000 more ticks -- racing with the scout timer.
    //
    // Let's use a processing time that guarantees the race is lost:
    // we already ticked 200,000 in Phase 2. The watchdog fires at
    // 500,000 total ticks from Phase 1's arm_watchdog(). We need
    // 300,000 more ticks. The scout timer needs 5000 ticks from Phase 3.
    // 5000 < 300,000, so the scout timer fires first in this case.
    //
    // To truly reproduce the bug, Phase 2 needs to tick 495,001+ ticks.
    // Let's redo with a tight timing.

    // Actually, the test above already works: tick past the scout timer.
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);

    // THE KEY CHECK: did the scout ack get generated?
    auto scout_ack = hs.receive_frame();
    CHECK(scout_ack.has_value());
    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutAckReceived);
}


TEST_CASE("FourWayHandshake: watchdog race -- processing time nearly equals watchdog",
          "[econet][handshake][rx-then-tx][watchdog]") {
    // Reproduces the exact race condition: the processing delay is just
    // under the watchdog timeout, so the stale watchdog fires shortly
    // after the reply TX starts, killing the scout ack timer.

    TestBackend backend;
    FourWayHandshake hs(backend);

    // === Phase 1: Complete RX handshake ===
    NetworkFrame incoming;
    incoming.type = FrameType::Unicast;
    incoming.dest_stn = 254;
    incoming.dest_net = 0;
    incoming.src_stn = 221;
    incoming.src_net = 0;
    incoming.control_byte = 0x00;
    incoming.port = 0x99;
    incoming.data = {0x90};
    backend.inject_rx_network_frame(incoming);

    hs.receive_frame();
    hs.send_frame(make_raw_frame({221, 0, 254, 0}));
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    hs.receive_frame();
    hs.send_frame(make_raw_frame({221, 0, 254, 0}));
    hs.tick();
    REQUIRE(hs.stage() == FourWayHandshake::Stage::Idle);

    // === Phase 2: Tick to just BEFORE the watchdog timeout ===
    // Leave only 1000 ticks remaining on the watchdog.
    // The scout ack timer needs 5000 ticks, so the watchdog fires first.
    int ticks_to_nearly_expire = FourWayHandshake::WATCHDOG_TIMEOUT
                                 - FourWayHandshake::SCOUT_ACK_TIMEOUT
                                 - 1  // the hs.tick() above
                                 - 1000;  // leave 1000 ticks
    tick_n(hs, ticks_to_nearly_expire);
    CHECK(hs.stage() == FourWayHandshake::Stage::Idle);

    backend.clear_sent_frames();

    // === Phase 3: Start reply TX ===
    hs.send_frame(make_raw_frame({221, 0, 254, 0, 0x80, 0x90}));
    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutSent);

    // === Phase 4: Tick 5000 for scout ack timer ===
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);

    // With the fix: scout ack timer fires, ack generated.
    // Without the fix: stale watchdog fires at tick ~1000,
    // resetting the handshake. Scout ack timer is killed.
    auto scout_ack = hs.receive_frame();
    INFO("Stage after scout ack timer: " << static_cast<int>(hs.stage()));
    CHECK(scout_ack.has_value());
    CHECK(hs.stage() == FourWayHandshake::Stage::ScoutAckReceived);
}


// =============================================================================
// Inbound holding queue (out-of-order and interleaved packets)
// =============================================================================
//
// UDP does not guarantee ordering, and nothing constrains a peer to speak only
// while we are listening for it. A packet that does not match the stage the
// handshake happens to be in must be held and re-offered, not destroyed --
// destroying it loses a fileserver reply, an inbound immediate, or another
// station's traffic outright. See docs/discussion/aun-robustness.md defect 1.

namespace {

// Drive the handshake through a TX transaction as far as DataSent, where it
// is waiting for the peer's Ack. This is the window in which a reply that
// overtook its own ack arrives.
void advance_to_data_sent(FourWayHandshake& hs) {
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0x80, 0x99}));
    tick_n(hs, FourWayHandshake::SCOUT_ACK_TIMEOUT);
    hs.receive_frame();  // consume the synthetic scout ack
    hs.send_frame(make_raw_frame({254, 0, 1, 0, 0xAA}));
    REQUIRE(hs.stage() == FourWayHandshake::Stage::DataSent);
}

// A Unicast as a fileserver would send its reply.
NetworkFrame make_reply_unicast(uint8_t from_stn = 254, uint8_t port = 0x90) {
    NetworkFrame reply;
    reply.type = FrameType::Unicast;
    reply.port = port;
    reply.control_byte = 0x00;
    reply.dest_net = 0;
    reply.dest_stn = 1;
    reply.src_net = 0;
    reply.src_stn = from_stn;
    reply.data = {0xDE, 0xAD};
    return reply;
}

// Pull frames until one arrives or the budget is exhausted, ticking as we go.
std::optional<NetworkFrame> drain_until_frame(FourWayHandshake& hs, int budget) {
    for (int i = 0; i < budget; ++i) {
        if (auto frame = hs.receive_frame()) return frame;
        hs.tick();
    }
    return std::nullopt;
}

}  // namespace

TEST_CASE("FourWayHandshake: reply that overtakes its ack is not lost",
          "[econet][handshake][holding]") {
    TestBackend backend;
    FourWayHandshake hs(backend);
    advance_to_data_sent(hs);

    // The peer's reply overtakes the ack for the data we just sent. Before
    // the holding queue this was read off the socket and destroyed, and the
    // guest waited out its OSWORD timeout for a reply that no longer existed.
    backend.inject_rx_network_frame(make_reply_unicast());
    NetworkFrame ack;
    ack.type = FrameType::Ack;
    backend.inject_rx_network_frame(ack);

    // The ack completes our transaction ...
    auto final_ack = drain_until_frame(hs, 200);
    REQUIRE(final_ack.has_value());
    CHECK(final_ack->data.size() == 4);   // synthetic final ack

    // ... and the held reply is then delivered as an inbound scout.
    auto scout = drain_until_frame(hs, FourWayHandshake::IDLE_COOLDOWN + 200);
    REQUIRE(scout.has_value());
    REQUIRE(scout->data.size() >= 6);
    CHECK(scout->data[DEST_STN] == 1);      // addressed to us
    CHECK(scout->data[SRC_STN] == 254);     // from the fileserver
    CHECK(scout->data[PORT] == 0x90);
    CHECK(hs.held_frames_redelivered_count() == 1);
}

TEST_CASE("FourWayHandshake: third station's traffic during a transaction survives",
          "[econet][handshake][holding]") {
    TestBackend backend;
    FourWayHandshake hs(backend);
    advance_to_data_sent(hs);

    // A station we are not talking to addresses us mid-transaction. On a
    // segment with more than two participants this is ordinary traffic, and
    // discarding it means the busier we are the more of everyone else's
    // frames we destroy.
    backend.inject_rx_network_frame(make_reply_unicast(/*from_stn=*/200,
                                                       /*port=*/0x91));
    NetworkFrame ack;
    ack.type = FrameType::Ack;
    backend.inject_rx_network_frame(ack);

    REQUIRE(drain_until_frame(hs, 200).has_value());  // our final ack

    auto scout = drain_until_frame(hs, FourWayHandshake::IDLE_COOLDOWN + 200);
    REQUIRE(scout.has_value());
    REQUIRE(scout->data.size() >= 6);
    CHECK(scout->data[SRC_STN] == 200);
    CHECK(scout->data[PORT] == 0x91);
}

TEST_CASE("FourWayHandshake: broadcast arriving mid-transaction survives",
          "[econet][handshake][holding]") {
    TestBackend backend;
    FourWayHandshake hs(backend);
    advance_to_data_sent(hs);

    NetworkFrame bcast;
    bcast.type = FrameType::Broadcast;
    bcast.port = 0x9C;
    bcast.control_byte = 0x00;
    bcast.dest_stn = 0xFF;
    bcast.dest_net = 0xFF;
    bcast.src_stn = 200;
    bcast.src_net = 0;
    bcast.data = {0x01, 0x02};
    backend.inject_rx_network_frame(bcast);

    NetworkFrame ack;
    ack.type = FrameType::Ack;
    backend.inject_rx_network_frame(ack);

    REQUIRE(drain_until_frame(hs, 200).has_value());  // our final ack

    auto delivered = drain_until_frame(hs, FourWayHandshake::IDLE_COOLDOWN + 200);
    REQUIRE(delivered.has_value());
    REQUIRE(delivered->data.size() >= 6);
    CHECK(delivered->data[DEST_STN] == 0xFF);
    CHECK(delivered->data[SRC_STN] == 200);
}

TEST_CASE("FourWayHandshake: held frames expire rather than accumulating",
          "[econet][handshake][holding]") {
    TestBackend backend;
    FourWayHandshake hs(backend);
    advance_to_data_sent(hs);

    // A frame nobody ever becomes ready for must not sit in the queue for
    // ever: it would eventually be delivered wildly out of context, long
    // after the conversation it belonged to ended.
    backend.inject_rx_network_frame(make_reply_unicast());
    hs.receive_frame();                       // parks it
    CHECK(hs.held_frame_count() == 1);

    tick_n(hs, FourWayHandshake::HELD_FRAME_TTL + 1);
    CHECK(hs.held_frame_count() == 0);
    CHECK(hs.held_frames_expired_count() == 1);
}

TEST_CASE("FourWayHandshake: holding queue is bounded",
          "[econet][handshake][holding]") {
    TestBackend backend;
    FourWayHandshake hs(backend);
    advance_to_data_sent(hs);

    // A peer that floods us -- broken, hostile, or just very busy -- must not
    // be able to grow our memory without bound.
    const int flood = FourWayHandshake::MAX_HELD_FRAMES * 3;
    for (int i = 0; i < flood; ++i) {
        backend.inject_rx_network_frame(make_reply_unicast());
        hs.receive_frame();
    }

    CHECK(hs.held_frame_count() <= FourWayHandshake::MAX_HELD_FRAMES);
    CHECK(hs.held_frames_dropped_count() > 0);
}

TEST_CASE("FourWayHandshake: reset clears held frames",
          "[econet][handshake][holding]") {
    TestBackend backend;
    FourWayHandshake hs(backend);
    advance_to_data_sent(hs);

    backend.inject_rx_network_frame(make_reply_unicast());
    hs.receive_frame();
    REQUIRE(hs.held_frame_count() == 1);

    hs.reset();
    CHECK(hs.held_frame_count() == 0);
}
