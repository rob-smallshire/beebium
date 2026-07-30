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

// Test gRPC EconetService
//
// These tests verify the EconetService implementation by acting as a gRPC client.
// They create a local server, connect to it, and verify Econet operations.

#include <catch2/catch_test_macros.hpp>

#include "beebium/Machines.hpp"
#include "beebium/service/Server.hpp"

#include "econet.grpc.pb.h"
#include <grpcpp/grpcpp.h>

#include <chrono>
#include <fstream>
#include <vector>

namespace {

std::vector<uint8_t> load_rom(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Cannot open ROM: " + filepath);
    }
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

class EconetTestFixture {
public:
    EconetTestFixture() {
#ifdef BEEBIUM_ROM_DIR
        auto mos = load_rom(std::string(BEEBIUM_ROM_DIR) + "/acorn-mos_1_20.rom");
        auto basic = load_rom(std::string(BEEBIUM_ROM_DIR) + "/bbc-basic_2.rom");
        std::copy(mos.begin(), mos.end(), machine_.state().memory.mos_rom.data());
        machine_.state().memory.load_basic(basic.data(), basic.size());
#endif
        machine_.reset();

        server_ = std::make_unique<beebium::service::Server<beebium::ModelB>>(
            machine_, "127.0.0.1", 0);
        server_->start({}, {});

        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        stub_ = beebium::EconetService::NewStub(channel_);
    }

    ~EconetTestFixture() {
        server_->stop();
    }

    beebium::ModelB& machine() { return machine_; }
    beebium::EconetService::Stub& stub() { return *stub_; }

private:
    beebium::ModelB machine_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::EconetService::Stub> stub_;
};

}  // namespace

// ============================================================================
// GetEconetStatus
// ============================================================================

TEST_CASE("EconetService GetEconetStatus when disabled", "[grpc][econet]") {
    EconetTestFixture fixture;

    grpc::ClientContext context;
    beebium::GetEconetStatusRequest request;
    beebium::GetEconetStatusResponse response;

    auto status = fixture.stub().GetEconetStatus(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.has_econet_socket());
    REQUIRE_FALSE(response.enabled());
    REQUIRE(response.station_id() == 0);
}

TEST_CASE("EconetService GetEconetStatus ADLC registers after enable", "[grpc][econet]") {
    EconetTestFixture fixture;

    // Enable first
    {
        grpc::ClientContext ctx;
        beebium::EnableEconetRequest req;
        beebium::EnableEconetResponse resp;
        req.set_station_id(5);
        req.set_no_network(true);
        fixture.stub().EnableEconet(&ctx, req, &resp);
        REQUIRE(resp.success());
    }

    grpc::ClientContext context;
    beebium::GetEconetStatusRequest request;
    beebium::GetEconetStatusResponse response;

    auto status = fixture.stub().GetEconetStatus(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.enabled());
    REQUIRE(response.station_id() == 5);
    REQUIRE(response.has_adlc());
    // After enable, ADLC should have valid register state
    CHECK(response.adlc().tx_frame_field() == "idle");
    CHECK(response.adlc().rx_frame_field() == "idle");
}

TEST_CASE("EconetService GetEconetStatus handshake stage", "[grpc][econet]") {
    EconetTestFixture fixture;

    // Enable with AUN (ephemeral port)
    {
        grpc::ClientContext ctx;
        beebium::EnableEconetRequest req;
        beebium::EnableEconetResponse resp;
        req.set_station_id(10);
        req.set_no_network(true);
        fixture.stub().EnableEconet(&ctx, req, &resp);
        REQUIRE(resp.success());
    }

    grpc::ClientContext context;
    beebium::GetEconetStatusRequest request;
    beebium::GetEconetStatusResponse response;

    auto status = fixture.stub().GetEconetStatus(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.aun_mode());
    REQUIRE(response.has_handshake());
    CHECK(response.handshake().stage() == "idle");
}

// ============================================================================
// EnableEconet
// ============================================================================

TEST_CASE("EconetService EnableEconet no_network", "[grpc][econet]") {
    EconetTestFixture fixture;

    grpc::ClientContext context;
    beebium::EnableEconetRequest request;
    beebium::EnableEconetResponse response;
    request.set_station_id(42);
    request.set_no_network(true);

    auto status = fixture.stub().EnableEconet(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.success());

    // Verify via GetEconetStatus
    grpc::ClientContext ctx2;
    beebium::GetEconetStatusRequest req2;
    beebium::GetEconetStatusResponse resp2;
    fixture.stub().GetEconetStatus(&ctx2, req2, &resp2);

    REQUIRE(resp2.enabled());
    REQUIRE(resp2.station_id() == 42);
    REQUIRE_FALSE(resp2.connected());
}

TEST_CASE("EconetService EnableEconet with AUN port", "[grpc][econet]") {
    EconetTestFixture fixture;

    grpc::ClientContext context;
    beebium::EnableEconetRequest request;
    beebium::EnableEconetResponse response;
    request.set_station_id(10);
    // Use port 0 which will give us the default (32768)
    // But that might conflict, so let's request a specific high port
    // Actually, use no specific port — the default of 0 in the proto
    // means "use default (32768)" per the proto definition.
    // To avoid port conflicts in tests, we should set no_network or
    // accept that the bind might fail if 32768 is in use.
    // Better: use an unlikely port.
    request.set_aun_port(0);  // Will use AUN_DEFAULT_PORT (32768)

    auto status = fixture.stub().EnableEconet(&context, request, &response);

    REQUIRE(status.ok());
    // Port binding may fail if 32768 is in use, skip further checks
    if (response.success()) {
        CHECK(response.actual_aun_port() > 0);

        // Verify connected
        grpc::ClientContext ctx2;
        beebium::GetEconetStatusRequest req2;
        beebium::GetEconetStatusResponse resp2;
        fixture.stub().GetEconetStatus(&ctx2, req2, &resp2);
        CHECK(resp2.connected());
        // aun_port / peer_count moved to AunService.GetStatus.
    }
}

TEST_CASE("EconetService EnableEconet invalid station 0", "[grpc][econet]") {
    EconetTestFixture fixture;

    grpc::ClientContext context;
    beebium::EnableEconetRequest request;
    beebium::EnableEconetResponse response;
    request.set_station_id(0);
    request.set_no_network(true);

    auto status = fixture.stub().EnableEconet(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE_FALSE(response.success());
    REQUIRE(response.error().find("between 1 and 254") != std::string::npos);
}

TEST_CASE("EconetService EnableEconet invalid station 255", "[grpc][econet]") {
    EconetTestFixture fixture;

    grpc::ClientContext context;
    beebium::EnableEconetRequest request;
    beebium::EnableEconetResponse response;
    request.set_station_id(255);
    request.set_no_network(true);

    auto status = fixture.stub().EnableEconet(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE_FALSE(response.success());
    REQUIRE(response.error().find("between 1 and 254") != std::string::npos);
}

TEST_CASE("EconetService EnableEconet when already enabled", "[grpc][econet]") {
    EconetTestFixture fixture;

    // Enable first
    {
        grpc::ClientContext ctx;
        beebium::EnableEconetRequest req;
        beebium::EnableEconetResponse resp;
        req.set_station_id(5);
        req.set_no_network(true);
        fixture.stub().EnableEconet(&ctx, req, &resp);
        REQUIRE(resp.success());
    }

    // Try again
    grpc::ClientContext context;
    beebium::EnableEconetRequest request;
    beebium::EnableEconetResponse response;
    request.set_station_id(10);
    request.set_no_network(true);

    auto status = fixture.stub().EnableEconet(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE_FALSE(response.success());
    REQUIRE(response.error().find("already enabled") != std::string::npos);
}

// ============================================================================
// DisableEconet
// ============================================================================

TEST_CASE("EconetService DisableEconet", "[grpc][econet]") {
    EconetTestFixture fixture;

    // Enable first
    {
        grpc::ClientContext ctx;
        beebium::EnableEconetRequest req;
        beebium::EnableEconetResponse resp;
        req.set_station_id(5);
        req.set_no_network(true);
        fixture.stub().EnableEconet(&ctx, req, &resp);
        REQUIRE(resp.success());
    }

    grpc::ClientContext context;
    beebium::DisableEconetRequest request;
    beebium::DisableEconetResponse response;

    auto status = fixture.stub().DisableEconet(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.success());

    // Verify disabled
    grpc::ClientContext ctx2;
    beebium::GetEconetStatusRequest req2;
    beebium::GetEconetStatusResponse resp2;
    fixture.stub().GetEconetStatus(&ctx2, req2, &resp2);
    REQUIRE_FALSE(resp2.enabled());
}

TEST_CASE("EconetService DisableEconet when already disabled", "[grpc][econet]") {
    EconetTestFixture fixture;

    grpc::ClientContext context;
    beebium::DisableEconetRequest request;
    beebium::DisableEconetResponse response;

    auto status = fixture.stub().DisableEconet(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE_FALSE(response.success());
    REQUIRE(response.error().find("not enabled") != std::string::npos);
}

// ============================================================================
// SetStationId
// ============================================================================

TEST_CASE("EconetService SetStationId success", "[grpc][econet]") {
    EconetTestFixture fixture;

    // Enable first
    {
        grpc::ClientContext ctx;
        beebium::EnableEconetRequest req;
        beebium::EnableEconetResponse resp;
        req.set_station_id(5);
        req.set_no_network(true);
        fixture.stub().EnableEconet(&ctx, req, &resp);
        REQUIRE(resp.success());
    }

    // Set station ID to a new value
    {
        grpc::ClientContext ctx;
        beebium::SetStationIdRequest req;
        beebium::SetStationIdResponse resp;
        req.set_station_id(200);

        auto status = fixture.stub().SetStationId(&ctx, req, &resp);
        REQUIRE(status.ok());
        REQUIRE(resp.success());
    }

    // Verify via GetEconetStatus
    {
        grpc::ClientContext ctx;
        beebium::GetEconetStatusRequest req;
        beebium::GetEconetStatusResponse resp;
        fixture.stub().GetEconetStatus(&ctx, req, &resp);
        REQUIRE(resp.station_id() == 200);
    }
}

TEST_CASE("EconetService SetStationId invalid zero", "[grpc][econet]") {
    EconetTestFixture fixture;

    // Enable first
    {
        grpc::ClientContext ctx;
        beebium::EnableEconetRequest req;
        beebium::EnableEconetResponse resp;
        req.set_station_id(5);
        req.set_no_network(true);
        fixture.stub().EnableEconet(&ctx, req, &resp);
        REQUIRE(resp.success());
    }

    grpc::ClientContext context;
    beebium::SetStationIdRequest request;
    beebium::SetStationIdResponse response;
    request.set_station_id(0);

    auto status = fixture.stub().SetStationId(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE_FALSE(response.success());
    REQUIRE(response.error().find("between 1 and 254") != std::string::npos);
}

TEST_CASE("EconetService SetStationId invalid 255", "[grpc][econet]") {
    EconetTestFixture fixture;

    // Enable first
    {
        grpc::ClientContext ctx;
        beebium::EnableEconetRequest req;
        beebium::EnableEconetResponse resp;
        req.set_station_id(5);
        req.set_no_network(true);
        fixture.stub().EnableEconet(&ctx, req, &resp);
        REQUIRE(resp.success());
    }

    grpc::ClientContext context;
    beebium::SetStationIdRequest request;
    beebium::SetStationIdResponse response;
    request.set_station_id(255);

    auto status = fixture.stub().SetStationId(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE_FALSE(response.success());
    REQUIRE(response.error().find("between 1 and 254") != std::string::npos);
}

TEST_CASE("EconetService SetStationId when disabled", "[grpc][econet]") {
    EconetTestFixture fixture;

    grpc::ClientContext context;
    beebium::SetStationIdRequest request;
    beebium::SetStationIdResponse response;
    request.set_station_id(42);

    auto status = fixture.stub().SetStationId(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE_FALSE(response.success());
    REQUIRE(response.error().find("not enabled") != std::string::npos);
}

// ============================================================================
// SubscribeEconetEvents
// ============================================================================

// ============================================================================
// WatchEconetStatus (streaming)
// ============================================================================

TEST_CASE("EconetService WatchEconetStatus initial push on disabled socket",
          "[grpc][econet]") {
    EconetTestFixture fixture;

    grpc::ClientContext context;
    beebium::WatchEconetStatusRequest request;
    auto reader = fixture.stub().WatchEconetStatus(&context, request);

    beebium::GetEconetStatusResponse snapshot;
    REQUIRE(reader->Read(&snapshot));
    REQUIRE(snapshot.has_econet_socket());
    REQUIRE_FALSE(snapshot.enabled());

    context.TryCancel();
    while (reader->Read(&snapshot)) {}
    (void)reader->Finish();
}

TEST_CASE("EconetService WatchEconetStatus pushes on enable",
          "[grpc][econet]") {
    EconetTestFixture fixture;

    grpc::ClientContext context;
    beebium::WatchEconetStatusRequest request;
    request.set_min_interval_ms(25);
    auto reader = fixture.stub().WatchEconetStatus(&context, request);

    beebium::GetEconetStatusResponse snapshot;
    REQUIRE(reader->Read(&snapshot));
    REQUIRE_FALSE(snapshot.enabled());

    // Trigger a state change by enabling Econet hardware.
    {
        grpc::ClientContext ctx;
        beebium::EnableEconetRequest req;
        beebium::EnableEconetResponse resp;
        req.set_station_id(42);
        req.set_no_network(true);
        fixture.stub().EnableEconet(&ctx, req, &resp);
        REQUIRE(resp.success());
    }

    REQUIRE(reader->Read(&snapshot));
    REQUIRE(snapshot.enabled());
    REQUIRE(snapshot.station_id() == 42);

    context.TryCancel();
    while (reader->Read(&snapshot)) {}
    (void)reader->Finish();
}

TEST_CASE("EconetService WatchEconetStatus pushes on station id change",
          "[grpc][econet]") {
    EconetTestFixture fixture;

    // Enable first so SetStationId has something to change.
    {
        grpc::ClientContext ctx;
        beebium::EnableEconetRequest req;
        beebium::EnableEconetResponse resp;
        req.set_station_id(5);
        req.set_no_network(true);
        fixture.stub().EnableEconet(&ctx, req, &resp);
        REQUIRE(resp.success());
    }

    grpc::ClientContext context;
    beebium::WatchEconetStatusRequest request;
    request.set_min_interval_ms(25);
    auto reader = fixture.stub().WatchEconetStatus(&context, request);

    beebium::GetEconetStatusResponse snapshot;
    REQUIRE(reader->Read(&snapshot));
    REQUIRE(snapshot.enabled());
    REQUIRE(snapshot.station_id() == 5);

    {
        grpc::ClientContext ctx;
        beebium::SetStationIdRequest req;
        beebium::SetStationIdResponse resp;
        req.set_station_id(123);
        fixture.stub().SetStationId(&ctx, req, &resp);
        REQUIRE(resp.success());
    }

    REQUIRE(reader->Read(&snapshot));
    REQUIRE(snapshot.station_id() == 123);

    context.TryCancel();
    while (reader->Read(&snapshot)) {}
    (void)reader->Finish();
}

TEST_CASE("EconetService WatchEconetStatus exits on client cancel",
          "[grpc][econet]") {
    EconetTestFixture fixture;

    grpc::ClientContext context;
    beebium::WatchEconetStatusRequest request;
    request.set_min_interval_ms(25);
    auto reader = fixture.stub().WatchEconetStatus(&context, request);

    beebium::GetEconetStatusResponse snapshot;
    REQUIRE(reader->Read(&snapshot));

    auto t0 = std::chrono::steady_clock::now();
    context.TryCancel();
    while (reader->Read(&snapshot)) {}
    auto status = reader->Finish();
    auto elapsed = std::chrono::steady_clock::now() - t0;

    REQUIRE(elapsed < std::chrono::seconds(2));
    REQUIRE((status.error_code() == grpc::StatusCode::CANCELLED ||
             status.error_code() == grpc::StatusCode::OK));
}

TEST_CASE("EconetService SubscribeEconetEvents refuses when no hardware is fitted",
          "[grpc][econet]") {
    // Without Econet hardware there is no backend chain and so nothing to
    // observe. Saying so is better than opening a stream that can only ever
    // be silent, which a caller cannot tell from a quiet network.
    EconetTestFixture fixture;

    grpc::ClientContext context;
    beebium::SubscribeEconetEventsRequest request;

    auto reader = fixture.stub().SubscribeEconetEvents(&context, request);
    beebium::EconetEvent event;
    REQUIRE_FALSE(reader->Read(&event));

    auto status = reader->Finish();
    REQUIRE(status.error_code() == grpc::StatusCode::FAILED_PRECONDITION);
}

TEST_CASE("EconetService SubscribeEconetEvents reports frames crossing the wire",
          "[grpc][econet]") {
    EconetTestFixture fixture;

    auto backend = std::make_unique<beebium::TestBackend>();
    auto* raw_backend = backend.get();
    fixture.machine().state().memory.econet_socket.enable(
        101, std::move(backend), /*aun_mode=*/false);

    grpc::ClientContext context;
    beebium::SubscribeEconetEventsRequest request;
    request.set_min_interval_ms(5);
    auto reader = fixture.stub().SubscribeEconetEvents(&context, request);

    // Give the subscriber a moment to establish its starting sequence, so the
    // frames below are genuinely "what happens next" rather than a race.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto& socket = fixture.machine().state().memory.econet_socket;

    // A frame the guest transmits, and one arriving from the network.
    beebium::NetworkFrame outgoing;
    outgoing.type = beebium::FrameType::Unicast;
    outgoing.port = 0x99;
    outgoing.control_byte = 0x80;
    outgoing.dest_net = 0;
    outgoing.dest_stn = 254;
    outgoing.src_net = 0;
    outgoing.src_stn = 101;
    outgoing.data = {0xDE, 0xAD, 0xBE, 0xEF};
    socket.backend_chain_for_test()->send_frame(outgoing);

    beebium::NetworkFrame incoming;
    incoming.type = beebium::FrameType::Ack;
    incoming.dest_stn = 101;
    incoming.src_stn = 254;
    raw_backend->inject_rx_network_frame(incoming);
    (void)socket.backend_chain_for_test()->receive_frame();

    beebium::EconetEvent sent;
    REQUIRE(reader->Read(&sent));
    CHECK(sent.type() == beebium::ECONET_EVENT_FRAME_SENT);
    CHECK(sent.frame().frame_type() == "unicast");
    CHECK(sent.frame().dest_stn() == 254);
    CHECK(sent.frame().src_stn() == 101);
    CHECK(sent.frame().port() == 0x99);
    CHECK(sent.frame().data_length() == 4);
    CHECK(sent.frame().data() == std::string("\xDE\xAD\xBE\xEF", 4));

    beebium::EconetEvent received;
    REQUIRE(reader->Read(&received));
    CHECK(received.type() == beebium::ECONET_EVENT_FRAME_RECEIVED);
    CHECK(received.frame().frame_type() == "ack");

    // Sequence numbers are monotonic, so a client can tell a gap from a lull.
    CHECK(received.sequence() == sent.sequence() + 1);

    context.TryCancel();
    (void)reader->Finish();
}

TEST_CASE("EconetService SubscribeEconetEvents truncates long payloads but "
          "reports their true size",
          "[grpc][econet]") {
    EconetTestFixture fixture;

    auto backend = std::make_unique<beebium::TestBackend>();
    fixture.machine().state().memory.econet_socket.enable(
        101, std::move(backend), /*aun_mode=*/false);

    grpc::ClientContext context;
    beebium::SubscribeEconetEventsRequest request;
    request.set_min_interval_ms(5);
    auto reader = fixture.stub().SubscribeEconetEvents(&context, request);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const std::size_t payload_size =
        beebium::ObservableBackend::MAX_PAYLOAD * 3;
    beebium::NetworkFrame big;
    big.type = beebium::FrameType::Unicast;
    big.dest_stn = 254;
    big.data.assign(payload_size, 0x5A);
    fixture.machine().state().memory.econet_socket
        .backend_chain_for_test()->send_frame(big);

    beebium::EconetEvent event;
    REQUIRE(reader->Read(&event));
    CHECK(event.frame().data_length() == payload_size);
    CHECK(event.frame().data().size() == beebium::ObservableBackend::MAX_PAYLOAD);

    context.TryCancel();
    (void)reader->Finish();
}
