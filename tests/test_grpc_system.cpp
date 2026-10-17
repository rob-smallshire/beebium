// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

// Test gRPC SystemService
//
// These tests verify the SystemService implementation by acting as a gRPC client.
// They create a local server, connect to it, and verify system info and provenance.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include "beebium/Machines.hpp"
#include "beebium/PacingClock.hpp"
#include "beebium/service/Server.hpp"
#include "beebium/service/SystemService.hpp"

#include "system.grpc.pb.h"
#include <grpcpp/grpcpp.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <limits>
#include <memory>
#include <random>
#include <vector>

namespace {

// Helper to load ROM file
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

// Generate a random UUID v4 for testing
std::string generate_test_uuid() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;

    uint64_t a = dis(gen);
    uint64_t b = dis(gen);

    // Set version 4 (0100) in bits 12-15 of time_hi_and_version
    a = (a & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    // Set variant (10) in bits 6-7 of clock_seq_hi_and_reserved
    b = (b & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

    char buf[37];
    std::snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%012llx",
             static_cast<uint32_t>(a >> 32),
             static_cast<uint16_t>(a >> 16),
             static_cast<uint16_t>(a),
             static_cast<uint16_t>(b >> 48),
             static_cast<unsigned long long>(b & 0x0000FFFFFFFFFFFFULL));
    return std::string(buf);
}

// Test fixture that sets up a machine and server with system service
class SystemTestFixture {
public:
    explicit SystemTestFixture(
        beebium::service::Provenance provenance = {},
        beebium::service::MachineIdentity identity = {},
        bool wire_pacing_clock = true,
        beebium::service::MachineStore* machine_store = nullptr)
        : provenance_(std::move(provenance))
        , identity_(std::move(identity))
        , wire_pacing_clock_(wire_pacing_clock) {

        // Apply default identity if not provided
        if (identity_.uuid.empty()) {
            identity_.uuid = generate_test_uuid();
        }
        if (identity_.name.empty()) {
            identity_.name = "BBC Model B";
        }
        if (identity_.model_type.empty()) {
            identity_.model_type = "model-b";
        }
        if (identity_.model_name.empty()) {
            identity_.model_name = "BBC Model B";
        }

        // Load ROMs
#ifdef BEEBIUM_ROM_DIR
        auto mos = load_rom(std::string(BEEBIUM_ROM_DIR) + "/acorn-mos_1_20.rom");
        auto basic = load_rom(std::string(BEEBIUM_ROM_DIR) + "/bbc-basic_2.rom");
        std::copy(mos.begin(), mos.end(), machine_.state().memory.mos_rom.data());
        machine_.state().memory.load_basic(basic.data(), basic.size());
#endif
        machine_.reset();

        // Start server on a dynamically allocated port
        server_ = std::make_unique<beebium::service::Server<beebium::ModelB>>(machine_, "127.0.0.1", 0);
        server_->set_machine_store(machine_store);
        server_->start(provenance_, identity_);

        // Wiring the clock is optional: leaving it unwired models the brief
        // startup window before the emulation loop creates the pacing clock,
        // during which the gRPC server is already accepting connections.
        if (wire_pacing_clock_) {
            pacing_clock_ = std::make_unique<beebium::PacingClock>(
                beebium::ModelB::Memory::default_pacing_config(),
                std::chrono::milliseconds(1),
                beebium::PlatformSleep{});
            server_->set_pacing_clock(pacing_clock_.get());
        }

        // Create client channel using the actual bound port
        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        system_stub_ = beebium::SystemService::NewStub(channel_);
    }

    ~SystemTestFixture() {
        server_->stop();
    }

    beebium::ModelB& machine() { return machine_; }
    beebium::SystemService::Stub& system() { return *system_stub_; }
    beebium::PacingClock& pacing_clock() { return *pacing_clock_; }

private:
    beebium::service::Provenance provenance_;
    beebium::service::MachineIdentity identity_;
    bool wire_pacing_clock_;
    beebium::ModelB machine_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::unique_ptr<beebium::PacingClock> pacing_clock_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::SystemService::Stub> system_stub_;
};

} // anonymous namespace

//////////////////////////////////////////////////////////////////////////////
// GetSystemInfo Tests (Legacy - verifying old fields are removed)
//////////////////////////////////////////////////////////////////////////////

// Note: machine_type and machine_display_name fields are now reserved
// and moved to the identity message. These tests verify the old fields
// return empty strings (proto3 behavior for reserved fields).

//////////////////////////////////////////////////////////////////////////////
// Provenance Tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("SystemService GetSystemInfo returns provenance when provided", "[grpc][system][provenance]") {
    beebium::service::Provenance prov{
        "python-client",
        "550e8400-e29b-41d4-a716-446655440000",
        "1.2.3",
        std::chrono::system_clock::from_time_t(1700000000)
    };
    SystemTestFixture fixture(prov);

    grpc::ClientContext context;
    beebium::GetSystemInfoRequest request;
    beebium::SystemInfo response;

    auto status = fixture.system().GetSystemInfo(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.has_provenance());

    const auto& returned_prov = response.provenance();
    CHECK(returned_prov.type() == "python-client");
    CHECK(returned_prov.instance_uuid() == "550e8400-e29b-41d4-a716-446655440000");
    CHECK(returned_prov.version() == "1.2.3");
    CHECK(returned_prov.timestamp() == 1700000000);
}

TEST_CASE("SystemService GetSystemInfo returns empty provenance when not provided", "[grpc][system][provenance]") {
    // Default empty provenance
    SystemTestFixture fixture;

    grpc::ClientContext context;
    beebium::GetSystemInfoRequest request;
    beebium::SystemInfo response;

    auto status = fixture.system().GetSystemInfo(&context, request, &response);

    REQUIRE(status.ok());
    // Provenance fields should be empty/default
    const auto& returned_prov = response.provenance();
    CHECK(returned_prov.type().empty());
    CHECK(returned_prov.instance_uuid().empty());
    CHECK(returned_prov.version().empty());
    CHECK(returned_prov.timestamp() == 0);
}

TEST_CASE("SystemService GetSystemInfo provenance type variations", "[grpc][system][provenance]") {
    SECTION("terminal type") {
        beebium::service::Provenance prov{"terminal", "uuid-1", "", std::chrono::system_clock::now()};
        SystemTestFixture fixture(prov);

        grpc::ClientContext context;
        beebium::GetSystemInfoRequest request;
        beebium::SystemInfo response;
        fixture.system().GetSystemInfo(&context, request, &response);

        CHECK(response.provenance().type() == "terminal");
    }

    SECTION("macos-gui type") {
        beebium::service::Provenance prov{"macos-gui", "uuid-2", "2.0.0", std::chrono::system_clock::now()};
        SystemTestFixture fixture(prov);

        grpc::ClientContext context;
        beebium::GetSystemInfoRequest request;
        beebium::SystemInfo response;
        fixture.system().GetSystemInfo(&context, request, &response);

        CHECK(response.provenance().type() == "macos-gui");
    }

    SECTION("typescript-oracle type") {
        beebium::service::Provenance prov{"typescript-oracle", "uuid-3", "0.1.0", std::chrono::system_clock::now()};
        SystemTestFixture fixture(prov);

        grpc::ClientContext context;
        beebium::GetSystemInfoRequest request;
        beebium::SystemInfo response;
        fixture.system().GetSystemInfo(&context, request, &response);

        CHECK(response.provenance().type() == "typescript-oracle");
    }
}

TEST_CASE("SystemService GetSystemInfo provenance timestamp conversion", "[grpc][system][provenance]") {
    // Test that chrono time_point is correctly converted to Unix timestamp
    auto now = std::chrono::system_clock::now();
    auto expected_seconds = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();

    beebium::service::Provenance prov{"test", "uuid", "1.0", now};
    SystemTestFixture fixture(prov);

    grpc::ClientContext context;
    beebium::GetSystemInfoRequest request;
    beebium::SystemInfo response;

    auto status = fixture.system().GetSystemInfo(&context, request, &response);

    REQUIRE(status.ok());
    // Allow 1 second tolerance for test execution time
    CHECK(response.provenance().timestamp() >= expected_seconds - 1);
    CHECK(response.provenance().timestamp() <= expected_seconds + 1);
}

TEST_CASE("SystemService GetSystemInfo provenance with empty version", "[grpc][system][provenance]") {
    beebium::service::Provenance prov{
        "unknown",
        "12345678-1234-1234-1234-123456789abc",
        "",  // Empty version is valid
        std::chrono::system_clock::now()
    };
    SystemTestFixture fixture(prov);

    grpc::ClientContext context;
    beebium::GetSystemInfoRequest request;
    beebium::SystemInfo response;

    auto status = fixture.system().GetSystemInfo(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.provenance().version().empty());
}

//////////////////////////////////////////////////////////////////////////////
// Machine Identity Tests
//////////////////////////////////////////////////////////////////////////////

// Helper to validate UUID format (RFC 4122)
bool is_valid_uuid(const std::string& uuid) {
    // UUID format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx (36 chars)
    if (uuid.length() != 36) return false;
    for (size_t i = 0; i < uuid.length(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (uuid[i] != '-') return false;
        } else {
            if (!std::isxdigit(uuid[i])) return false;
        }
    }
    return true;
}

TEST_CASE("SystemService GetSystemInfo returns identity", "[grpc][system][identity]") {
    SystemTestFixture fixture;

    grpc::ClientContext context;
    beebium::GetSystemInfoRequest request;
    beebium::SystemInfo response;

    auto status = fixture.system().GetSystemInfo(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.has_identity());

    const auto& identity = response.identity();
    CHECK(is_valid_uuid(identity.uuid()));
    CHECK_FALSE(identity.name().empty());
    CHECK(identity.model_type() == "model-b");
    CHECK_FALSE(identity.model_name().empty());
    CHECK(identity.model_name().find("BBC") != std::string::npos);
    CHECK(identity.model_name().find("Model B") != std::string::npos);
}

TEST_CASE("SystemService GetSystemInfo identity has stable UUID", "[grpc][system][identity]") {
    SystemTestFixture fixture;

    // First call
    grpc::ClientContext context1;
    beebium::GetSystemInfoRequest request1;
    beebium::SystemInfo response1;
    REQUIRE(fixture.system().GetSystemInfo(&context1, request1, &response1).ok());

    // Second call
    grpc::ClientContext context2;
    beebium::GetSystemInfoRequest request2;
    beebium::SystemInfo response2;
    REQUIRE(fixture.system().GetSystemInfo(&context2, request2, &response2).ok());

    // UUID should be identical across calls
    CHECK(response1.identity().uuid() == response2.identity().uuid());
}

// Note: machine_type and machine_display_name fields are now reserved in SystemInfo.
// Reserved fields don't generate accessors in proto3, so there's nothing to test.
// The identity message (with model_type and model_name) replaces these fields.

TEST_CASE("SystemService SetMachineName changes name", "[grpc][system][identity]") {
    SystemTestFixture fixture;

    // Get initial identity
    grpc::ClientContext context1;
    beebium::GetSystemInfoRequest info_request;
    beebium::SystemInfo info_response;
    REQUIRE(fixture.system().GetSystemInfo(&context1, info_request, &info_response).ok());
    auto initial_uuid = info_response.identity().uuid();

    // Change name
    grpc::ClientContext context2;
    beebium::SetMachineNameRequest name_request;
    name_request.set_name_template("My BBC Micro");
    beebium::SetMachineNameResponse name_response;

    auto status = fixture.system().SetMachineName(&context2, name_request, &name_response);

    REQUIRE(status.ok());
    CHECK(name_response.identity().name() == "My BBC Micro");
    CHECK(name_response.identity().uuid() == initial_uuid);  // UUID unchanged
}

TEST_CASE("SystemService SetMachineName persists across GetSystemInfo calls", "[grpc][system][identity]") {
    SystemTestFixture fixture;

    // Change name
    grpc::ClientContext context1;
    beebium::SetMachineNameRequest name_request;
    name_request.set_name_template("Test Server");
    beebium::SetMachineNameResponse name_response;
    REQUIRE(fixture.system().SetMachineName(&context1, name_request, &name_response).ok());

    // Verify via GetSystemInfo
    grpc::ClientContext context2;
    beebium::GetSystemInfoRequest info_request;
    beebium::SystemInfo info_response;
    REQUIRE(fixture.system().GetSystemInfo(&context2, info_request, &info_response).ok());

    CHECK(info_response.identity().name() == "Test Server");
}

TEST_CASE("SystemService SetMachineName rejects empty name", "[grpc][system][identity]") {
    SystemTestFixture fixture;

    grpc::ClientContext context;
    beebium::SetMachineNameRequest request;
    request.set_name_template("");  // Empty
    beebium::SetMachineNameResponse response;

    auto status = fixture.system().SetMachineName(&context, request, &response);

    CHECK_FALSE(status.ok());
    CHECK(status.error_code() == grpc::StatusCode::INVALID_ARGUMENT);
}

TEST_CASE("SystemService SetMachineName preserves model_type and model_name", "[grpc][system][identity]") {
    SystemTestFixture fixture;

    // Get initial identity
    grpc::ClientContext context1;
    beebium::GetSystemInfoRequest info_request;
    beebium::SystemInfo info_response;
    REQUIRE(fixture.system().GetSystemInfo(&context1, info_request, &info_response).ok());
    auto initial_model_type = info_response.identity().model_type();
    auto initial_model_name = info_response.identity().model_name();

    // Change name
    grpc::ClientContext context2;
    beebium::SetMachineNameRequest name_request;
    name_request.set_name_template("New Name");
    beebium::SetMachineNameResponse name_response;
    REQUIRE(fixture.system().SetMachineName(&context2, name_request, &name_response).ok());

    // Verify model_type and model_name unchanged
    CHECK(name_response.identity().model_type() == initial_model_type);
    CHECK(name_response.identity().model_name() == initial_model_name);
}

TEST_CASE("SystemService SetSpeedMultiplier accepts unlimited mode", "[grpc][system][pacing]") {
    SystemTestFixture fixture;

    grpc::ClientContext context;
    beebium::SetSpeedMultiplierRequest request;
    request.set_speed_multiplier(0.0);
    beebium::SetSpeedMultiplierResponse response;

    auto status = fixture.system().SetSpeedMultiplier(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.speed_multiplier() == 0.0);
    CHECK(fixture.pacing_clock().speed_multiplier() == 0.0);
}

TEST_CASE("SystemService SetSpeedMultiplier accepts finite positive values", "[grpc][system][pacing]") {
    SystemTestFixture fixture;

    grpc::ClientContext context;
    beebium::SetSpeedMultiplierRequest request;
    request.set_speed_multiplier(2.0);
    beebium::SetSpeedMultiplierResponse response;

    auto status = fixture.system().SetSpeedMultiplier(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.speed_multiplier() == 2.0);
    CHECK(fixture.pacing_clock().speed_multiplier() == 2.0);
}

TEST_CASE("SystemService SetSpeedMultiplier rejects negative values", "[grpc][system][pacing]") {
    SystemTestFixture fixture;

    grpc::ClientContext context;
    beebium::SetSpeedMultiplierRequest request;
    request.set_speed_multiplier(-1.0);
    beebium::SetSpeedMultiplierResponse response;

    auto status = fixture.system().SetSpeedMultiplier(&context, request, &response);

    CHECK_FALSE(status.ok());
    CHECK(status.error_code() == grpc::StatusCode::INVALID_ARGUMENT);
    CHECK(fixture.pacing_clock().speed_multiplier() == 1.0);
}

TEST_CASE("SystemService SetSpeedMultiplier rejects non-finite values", "[grpc][system][pacing]") {
    SystemTestFixture fixture;

    const double non_finite =
        GENERATE(std::numeric_limits<double>::quiet_NaN(),
                 std::numeric_limits<double>::infinity(),
                 -std::numeric_limits<double>::infinity());

    grpc::ClientContext context;
    beebium::SetSpeedMultiplierRequest request;
    request.set_speed_multiplier(non_finite);
    beebium::SetSpeedMultiplierResponse response;

    auto status = fixture.system().SetSpeedMultiplier(&context, request, &response);

    CHECK_FALSE(status.ok());
    CHECK(status.error_code() == grpc::StatusCode::INVALID_ARGUMENT);
    CHECK(fixture.pacing_clock().speed_multiplier() == 1.0);
}

TEST_CASE("SystemService GetPacingStats reports the configured speed multiplier",
          "[grpc][system][pacing]") {
    SystemTestFixture fixture;

    grpc::ClientContext set_ctx;
    beebium::SetSpeedMultiplierRequest set_request;
    set_request.set_speed_multiplier(2.0);
    beebium::SetSpeedMultiplierResponse set_response;
    REQUIRE(fixture.system()
                .SetSpeedMultiplier(&set_ctx, set_request, &set_response)
                .ok());

    grpc::ClientContext context;
    beebium::GetPacingStatsRequest request;
    beebium::PacingStats stats;
    REQUIRE(fixture.system().GetPacingStats(&context, request, &stats).ok());

    CHECK(stats.speed_multiplier() == 2.0);
    // The emulation loop is not running in this fixture, so no throughput
    // sample has been published: the headroom fields read as "no estimate".
    CHECK(stats.achieved_speed_multiplier() == 0.0);
    CHECK(stats.estimated_max_speed_multiplier() == 0.0);
}

// The pacing clock is wired only once the emulation loop starts, but the gRPC
// server accepts connections before that. The configured speed multiplier must
// remain queryable and settable throughout that startup window, rather than
// failing with UNAVAILABLE and racing fast clients.

TEST_CASE("SystemService GetPacingStats reports configured speed before the clock is wired",
          "[grpc][system][pacing]") {
    SystemTestFixture fixture({}, {}, /*wire_pacing_clock=*/false);

    grpc::ClientContext context;
    beebium::GetPacingStatsRequest request;
    beebium::PacingStats stats;
    auto status = fixture.system().GetPacingStats(&context, request, &stats);

    REQUIRE(status.ok());
    // Default configured multiplier with no clock: runtime fields are zero.
    CHECK(stats.speed_multiplier() == 1.0);
    CHECK(stats.achieved_speed_multiplier() == 0.0);
    CHECK(stats.estimated_max_speed_multiplier() == 0.0);
}

TEST_CASE("SystemService SetSpeedMultiplier works before the clock is wired",
          "[grpc][system][pacing]") {
    SystemTestFixture fixture({}, {}, /*wire_pacing_clock=*/false);

    grpc::ClientContext set_ctx;
    beebium::SetSpeedMultiplierRequest set_request;
    set_request.set_speed_multiplier(2.0);
    beebium::SetSpeedMultiplierResponse set_response;
    auto set_status =
        fixture.system().SetSpeedMultiplier(&set_ctx, set_request, &set_response);

    REQUIRE(set_status.ok());
    CHECK(set_response.speed_multiplier() == 2.0);

    // The value is retained and reported back without a clock present.
    grpc::ClientContext get_ctx;
    beebium::GetPacingStatsRequest get_request;
    beebium::PacingStats stats;
    REQUIRE(fixture.system().GetPacingStats(&get_ctx, get_request, &stats).ok());
    CHECK(stats.speed_multiplier() == 2.0);
}

TEST_CASE("SystemService SetSpeedMultiplier still validates before the clock is wired",
          "[grpc][system][pacing]") {
    SystemTestFixture fixture({}, {}, /*wire_pacing_clock=*/false);

    grpc::ClientContext context;
    beebium::SetSpeedMultiplierRequest request;
    request.set_speed_multiplier(-1.0);
    beebium::SetSpeedMultiplierResponse response;

    auto status = fixture.system().SetSpeedMultiplier(&context, request, &response);

    CHECK_FALSE(status.ok());
    CHECK(status.error_code() == grpc::StatusCode::INVALID_ARGUMENT);
}

// Note: WatchServerStatus is a streaming RPC that requires special handling
// for testing. The server-side implementation sends READY immediately and then
// blocks waiting for shutdown or client disconnect. Testing this properly
// requires either async clients or multi-threaded test setup.
// For now, this functionality is covered by Python integration tests.

TEST_CASE("A machine's advertisement names its model as well as its type",
          "[grpc][system][discovery]") {
    beebium::service::MachineIdentity identity;
    identity.uuid = "0f6c2a3e-1111-4222-8333-444455556666";
    identity.name = "Lab Beeb";
    identity.model_type = "ModelB";
    identity.model_name = "BBC Model B";
    beebium::service::Provenance provenance;
    provenance.type = "cli";

    const auto txt = beebium::service::machine_txt_records(identity, provenance);

    CHECK(txt.at("uuid") == "0f6c2a3e-1111-4222-8333-444455556666");
    CHECK(txt.at("role") == "host");
    CHECK(txt.at("model") == "ModelB");
    CHECK(txt.at("model_name") == "BBC Model B");
    CHECK(txt.at("provenance") == "cli");
    CHECK_FALSE(txt.contains("econet_station"));
}

//////////////////////////////////////////////////////////////////////////////
// Saving the machine (SaveMachine, ResetBattery, SystemInfo, status)
//////////////////////////////////////////////////////////////////////////////

namespace {

// A store that records what it is asked and answers as told.
class FakeMachineStore final : public beebium::service::MachineStore {
public:
    std::string machine_dirpath() const override { return dirpath; }
    bool machine_saved() const override { return saved; }
    std::string document_name() const override { return saved ? document : std::string(); }
    bool template_media_written() const override { return written; }
    SaveOutcome save(const std::string& destination, bool replace_existing,
                     const std::string& uuid, const std::string& name_template) override {
        asked_destination = destination;
        asked_replace = replace_existing;
        if (outcome.error == SaveOutcome::Error::None) saved = true;
        asked_uuid = uuid;
        asked_name_template = name_template;
        return outcome;
    }
    std::vector<std::string> reset_battery() override { return {"integra-b-rtc", "clock/saf3019p"}; }
    void renamed(const std::string& name_template) override { renamed_to = name_template; }

    std::string dirpath = "/machines/transient";
    std::string document = "Elite";
    bool saved = false;
    bool written = true;
    SaveOutcome outcome;
    std::string asked_destination, asked_uuid, asked_name_template, renamed_to;
    bool asked_replace = false;
};

}  // namespace

TEST_CASE("SystemService without a machine store cannot save", "[grpc][system][save]") {
    SystemTestFixture fixture;
    {
        grpc::ClientContext context;
        beebium::SystemInfo info;
        REQUIRE(fixture.system().GetSystemInfo(&context, {}, &info).ok());
        CHECK(info.machine_dirpath().empty());
        CHECK_FALSE(info.machine_saved());
    }
    grpc::ClientContext save_context;
    beebium::SaveMachineResponse saved;
    CHECK(fixture.system().SaveMachine(&save_context, {}, &saved).error_code() ==
          grpc::StatusCode::FAILED_PRECONDITION);
    grpc::ClientContext reset_context;
    beebium::ResetBatteryResponse reset;
    CHECK(fixture.system().ResetBattery(&reset_context, {}, &reset).error_code() ==
          grpc::StatusCode::FAILED_PRECONDITION);
}

TEST_CASE("SystemService reports and saves the machine through its store", "[grpc][system][save]") {
    FakeMachineStore store;
    beebium::service::MachineIdentity identity;
    identity.uuid = "11111111-1111-4111-8111-111111111111";
    identity.name_template = "Elite {model}";
    SystemTestFixture fixture({}, identity, true, &store);

    {
        grpc::ClientContext context;
        beebium::SystemInfo info;
        REQUIRE(fixture.system().GetSystemInfo(&context, {}, &info).ok());
        CHECK(info.machine_dirpath() == "/machines/transient");
        CHECK_FALSE(info.machine_saved());
    }

    SECTION("each refusal is its status") {
        using Error = beebium::service::MachineStore::SaveOutcome::Error;
        for (auto [error, code] : {std::pair{Error::InvalidArgument, grpc::StatusCode::INVALID_ARGUMENT},
                                   std::pair{Error::AlreadyExists, grpc::StatusCode::ALREADY_EXISTS},
                                   std::pair{Error::FailedPrecondition, grpc::StatusCode::FAILED_PRECONDITION},
                                   std::pair{Error::Internal, grpc::StatusCode::INTERNAL}}) {
            store.outcome = {error, "why", {}, {}};
            grpc::ClientContext context;
            beebium::SaveMachineResponse response;
            const auto status = fixture.system().SaveMachine(&context, {}, &response);
            CHECK(status.error_code() == code);
            CHECK(status.error_message() == "why");
        }
    }

    SECTION("a save is asked with the identity now, and a new uuid is adopted") {
        store.outcome = {{}, {}, "/home/me/Elite.beebium-machine",
                         "22222222-2222-4222-8222-222222222222"};
        grpc::ClientContext context;
        beebium::SaveMachineRequest request;
        request.set_destination_dirpath("/home/me/Elite.beebium-machine");
        request.set_replace_existing(true);
        beebium::SaveMachineResponse response;
        REQUIRE(fixture.system().SaveMachine(&context, request, &response).ok());
        CHECK(store.asked_destination == "/home/me/Elite.beebium-machine");
        CHECK(store.asked_replace);
        CHECK(store.asked_uuid == "11111111-1111-4111-8111-111111111111");
        CHECK(store.asked_name_template == "Elite {model}");
        CHECK(response.machine_dirpath() == "/home/me/Elite.beebium-machine");
        CHECK(response.identity().uuid() == "22222222-2222-4222-8222-222222222222");

        grpc::ClientContext info_context;
        beebium::SystemInfo info;
        REQUIRE(fixture.system().GetSystemInfo(&info_context, {}, &info).ok());
        CHECK(info.identity().uuid() == "22222222-2222-4222-8222-222222222222");
    }

    SECTION("a rename is recorded by the store") {
        grpc::ClientContext context;
        beebium::SetMachineNameRequest request;
        request.set_name_template("Renamed");
        beebium::SetMachineNameResponse response;
        REQUIRE(fixture.system().SetMachineName(&context, request, &response).ok());
        CHECK(store.renamed_to == "Renamed");
    }

    SECTION("a battery reset names the units forgotten") {
        grpc::ClientContext context;
        beebium::ResetBatteryResponse response;
        REQUIRE(fixture.system().ResetBattery(&context, {}, &response).ok());
        REQUIRE(response.reset_units_size() == 2);
        CHECK(response.reset_units(1) == "clock/saf3019p");
    }

    SECTION("every status event says whether template media were written") {
        grpc::ClientContext context;
        auto reader = fixture.system().WatchServerStatus(&context, {});
        beebium::ServerStatusEvent event;
        REQUIRE(reader->Read(&event));
        CHECK(event.status() == beebium::SERVER_STATUS_READY);
        CHECK(event.template_media_written());
        context.TryCancel();
        while (reader->Read(&event)) {}
        reader->Finish();
    }
}

TEST_CASE("SystemService names a saved machine by its document", "[grpc][system][save][name]") {
    auto identity_named = [](std::string name_template) {
        beebium::service::MachineIdentity identity;
        identity.name_template = std::move(name_template);
        return identity;
    };
    auto name_of = [](SystemTestFixture& fixture) {
        grpc::ClientContext context;
        beebium::SystemInfo info;
        REQUIRE(fixture.system().GetSystemInfo(&context, {}, &info).ok());
        return std::make_pair(info.identity().name(), info.identity().name_template());
    };

    SECTION("a template that does not place {document} gives way to it") {
        FakeMachineStore store;
        store.saved = true;
        SystemTestFixture fixture({}, identity_named("BBC Model B {machine-ordinal}"), true, &store);
        CHECK(name_of(fixture) == std::make_pair(std::string("Elite"),
                                                 std::string("BBC Model B {machine-ordinal}")));
    }
    SECTION("a template that places {document} is rendered as written") {
        FakeMachineStore store;
        store.saved = true;
        SystemTestFixture fixture({}, identity_named("{document} ({machine-model})"), true, &store);
        CHECK(name_of(fixture).first == "Elite (BBC Model B)");
    }
    SECTION("a machine not yet saved keeps its template") {
        FakeMachineStore store;
        SystemTestFixture fixture({}, identity_named("Station 80"), true, &store);
        CHECK(name_of(fixture).first == "Station 80");
    }
    SECTION("its first save gives it its document's name, announced as an identity change") {
        FakeMachineStore store;
        store.outcome = {{}, {}, "/home/me/Elite.beebium-machine", ""};
        beebium::service::MachineIdentity identity = identity_named("Station 80");
        identity.uuid = "11111111-1111-4111-8111-111111111111";
        store.outcome.machine_uuid = identity.uuid;
        SystemTestFixture fixture({}, identity, true, &store);

        grpc::ClientContext watch_context;
        auto reader = fixture.system().WatchServerStatus(&watch_context, {});
        beebium::ServerStatusEvent event;
        REQUIRE(reader->Read(&event));  // READY

        grpc::ClientContext context;
        beebium::SaveMachineRequest request;
        request.set_destination_dirpath("/home/me/Elite.beebium-machine");
        beebium::SaveMachineResponse response;
        REQUIRE(fixture.system().SaveMachine(&context, request, &response).ok());
        CHECK(response.identity().name() == "Elite");
        CHECK(response.identity().name_template() == "Station 80");

        bool announced = false;
        while (!announced && reader->Read(&event)) {
            if (event.status() == beebium::SERVER_STATUS_IDENTITY_CHANGED) {
                CHECK(event.identity().name() == "Elite");
                announced = true;
            }
        }
        CHECK(announced);
        watch_context.TryCancel();
        while (reader->Read(&event)) {}
        reader->Finish();
    }
}
