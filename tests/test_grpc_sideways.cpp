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

// Test gRPC SidewaysService
//
// These tests verify the sideways ROM/RAM service implementation by acting as a gRPC client.
// They create a local server, connect to it, and verify sideways memory functionality.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "beebium/Machines.hpp"
#include "beebium/service/Server.hpp"

#include "sideways.grpc.pb.h"
#include "debugger.grpc.pb.h"
#include <grpcpp/grpcpp.h>

#include <fstream>
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

// Test fixture for Model B (aliased sideways with 4 sockets)
class ModelBSidewaysFixture {
public:
    ModelBSidewaysFixture() {
#ifdef BEEBIUM_ROM_DIR
        auto mos = load_rom(std::string(BEEBIUM_ROM_DIR) + "/acorn-mos_1_20.rom");
        auto basic = load_rom(std::string(BEEBIUM_ROM_DIR) + "/bbc-basic_2.rom");
        std::copy(mos.begin(), mos.end(), machine_.state().memory.mos_rom.data());
        machine_.state().memory.load_basic(basic.data(), basic.size());
#endif
        machine_.reset();

        server_ = std::make_unique<beebium::service::Server<beebium::ModelB>>(machine_, "127.0.0.1", 0);
        server_->start({}, {});

        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        sideways_stub_ = beebium::SidewaysService::NewStub(channel_);
    }

    ~ModelBSidewaysFixture() {
        server_->stop();
    }

    beebium::ModelB& machine() { return machine_; }
    beebium::SidewaysService::Stub& sideways() { return *sideways_stub_; }

private:
    beebium::ModelB machine_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::SidewaysService::Stub> sideways_stub_;
};

// Test fixture for ROM/RAM board (16 independent slots)
class RomRamBoardSidewaysFixture {
public:
    RomRamBoardSidewaysFixture() {
#ifdef BEEBIUM_ROM_DIR
        auto mos = load_rom(std::string(BEEBIUM_ROM_DIR) + "/acorn-mos_1_20.rom");
        auto basic = load_rom(std::string(BEEBIUM_ROM_DIR) + "/bbc-basic_2.rom");
        std::copy(mos.begin(), mos.end(), machine_.state().memory.mos_rom.data());
        machine_.state().memory.load_basic(basic.data(), basic.size());
#endif
        machine_.reset();

        server_ = std::make_unique<beebium::service::Server<beebium::ModelBRomRamBoard>>(machine_, "127.0.0.1", 0);
        server_->start({}, {});

        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        sideways_stub_ = beebium::SidewaysService::NewStub(channel_);
        debugger_stub_ = beebium::DebuggerControl::NewStub(channel_);
    }

    ~RomRamBoardSidewaysFixture() {
        server_->stop();
    }

    beebium::ModelBRomRamBoard& machine() { return machine_; }
    beebium::SidewaysService::Stub& sideways() { return *sideways_stub_; }
    beebium::DebuggerControl::Stub& debugger() { return *debugger_stub_; }

private:
    beebium::ModelBRomRamBoard machine_;
    std::unique_ptr<beebium::service::Server<beebium::ModelBRomRamBoard>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::SidewaysService::Stub> sideways_stub_;
    std::unique_ptr<beebium::DebuggerControl::Stub> debugger_stub_;
};

// Test fixture for the ATPL Sidewise board (16 slots; slot 15 fitted as RAM).
class AtplSidewiseSidewaysFixture {
public:
    AtplSidewiseSidewaysFixture() {
#ifdef BEEBIUM_ROM_DIR
        auto mos = load_rom(std::string(BEEBIUM_ROM_DIR) + "/acorn-mos_1_20.rom");
        auto basic = load_rom(std::string(BEEBIUM_ROM_DIR) + "/bbc-basic_2.rom");
        std::copy(mos.begin(), mos.end(), machine_.state().memory.mos_rom.data());
        machine_.state().memory.load_basic(basic.data(), basic.size());  // slot 14
#endif
        machine_.state().memory.configure_slot_as_ram(15);  // fit RAM in slot 15
        machine_.reset();

        server_ = std::make_unique<beebium::service::Server<beebium::ModelBAtplSidewise>>(
            machine_, "127.0.0.1", 0);
        server_->start({}, {});

        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        sideways_stub_ = beebium::SidewaysService::NewStub(channel_);
    }

    ~AtplSidewiseSidewaysFixture() { server_->stop(); }

    beebium::ModelBAtplSidewise& machine() { return machine_; }
    beebium::SidewaysService::Stub& sideways() { return *sideways_stub_; }

private:
    beebium::ModelBAtplSidewise machine_;
    std::unique_ptr<beebium::service::Server<beebium::ModelBAtplSidewise>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::SidewaysService::Stub> sideways_stub_;
};

// Fixture for the Watford ROM/RAM board (16 slots; banks 0-7 RAM; the &FF30
// write-select latch and the S1/S2 board protection switches).
class WatfordRomRamSidewaysFixture {
public:
    WatfordRomRamSidewaysFixture() {
        for (uint8_t bank = 0; bank <= 7; ++bank) {
            machine_.state().memory.configure_slot_as_ram(bank);
        }
        machine_.reset();

        server_ = std::make_unique<beebium::service::Server<beebium::ModelBWatfordRomRam>>(
            machine_, "127.0.0.1", 0);
        server_->start({}, {});

        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        sideways_stub_ = beebium::SidewaysService::NewStub(channel_);
    }

    ~WatfordRomRamSidewaysFixture() { server_->stop(); }

    beebium::ModelBWatfordRomRam& machine() { return machine_; }
    beebium::SidewaysService::Stub& sideways() { return *sideways_stub_; }

private:
    beebium::ModelBWatfordRomRam machine_;
    std::unique_ptr<beebium::service::Server<beebium::ModelBWatfordRomRam>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::SidewaysService::Stub> sideways_stub_;
};

// Fixture for the Integra-B board (16 slots; banks 4-7 board RAM; per-chip
// write-protect switches). Optionally fits a 32K RAM chip in socket pair 8/9.
class IntegraBSidewaysFixture {
public:
    explicit IntegraBSidewaysFixture(bool pair_8_9_ram = false) {
        if (pair_8_9_ram) {
            machine_.state().memory.configure_slot_as_ram(8);
            machine_.state().memory.configure_slot_as_ram(9);
        }
        machine_.reset();

        server_ = std::make_unique<beebium::service::Server<beebium::ModelBIntegraB>>(
            machine_, "127.0.0.1", 0);
        server_->start({}, {});

        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        sideways_stub_ = beebium::SidewaysService::NewStub(channel_);
    }

    ~IntegraBSidewaysFixture() { server_->stop(); }

    beebium::ModelBIntegraB& machine() { return machine_; }
    beebium::SidewaysService::Stub& sideways() { return *sideways_stub_; }

private:
    beebium::ModelBIntegraB machine_;
    std::unique_ptr<beebium::service::Server<beebium::ModelBIntegraB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::SidewaysService::Stub> sideways_stub_;
};

// Fixture for Model B+ 128K with optional motherboard link override.
// Like the 64K but with four integral sideways RAM banks (W=slot 12,
// X=slot 13, Y/Z opposite IC71 per S13).
class ModelBPlus128KSidewaysFixture {
public:
    explicit ModelBPlus128KSidewaysFixture(
        beebium::ModelBPlus128KHardware::MotherboardLinks links =
            beebium::ModelBPlus128KHardware::MotherboardLinks{}) {
#ifdef BEEBIUM_ROM_DIR
        auto mos = load_rom(std::string(BEEBIUM_ROM_DIR) + "/acorn-mos_2_0.rom");
        auto basic = load_rom(std::string(BEEBIUM_ROM_DIR) + "/bbc-basic_2.rom");
        auto dfs = load_rom(std::string(BEEBIUM_ROM_DIR) + "/acorn-dfs_2_26.rom");
        std::copy(mos.begin(), mos.end(), machine_.state().memory.mos_rom.data());
        machine_.state().memory.load_sideways_rom(15, basic.data(), basic.size());
        machine_.state().memory.load_sideways_rom(11, dfs.data(), dfs.size());
#endif
        machine_.reset();
        machine_.state().memory.apply_motherboard_links(links);
        server_ = std::make_unique<beebium::service::Server<beebium::ModelBPlus128K>>(
            machine_, "127.0.0.1", 0);
        server_->set_motherboard_links(links);
        server_->start({}, {});

        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        sideways_stub_ = beebium::SidewaysService::NewStub(channel_);
    }

    ~ModelBPlus128KSidewaysFixture() { server_->stop(); }

    beebium::ModelBPlus128K& machine() { return machine_; }
    beebium::SidewaysService::Stub& sideways() { return *sideways_stub_; }

private:
    beebium::ModelBPlus128K machine_;
    std::unique_ptr<beebium::service::Server<beebium::ModelBPlus128K>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::SidewaysService::Stub> sideways_stub_;
};

// Fixture for Model B+ with optional motherboard link override. The B+
// has S13, which reroutes IC71 (BASIC) between slots 14/15 (South) and
// 0/1 (North).
class ModelBPlusSidewaysFixture {
public:
    explicit ModelBPlusSidewaysFixture(
        beebium::ModelBPlusHardware::MotherboardLinks links =
            beebium::ModelBPlusHardware::MotherboardLinks{}) {
#ifdef BEEBIUM_ROM_DIR
        // A stock Model B+ ships with integral BASIC (IC71) and DFS (IC68);
        // the preset loads those at boot. The fixture mirrors that so that
        // GetSlotStatus has something realistic to report.
        auto mos = load_rom(std::string(BEEBIUM_ROM_DIR) + "/acorn-mos_1_20.rom");
        auto basic = load_rom(std::string(BEEBIUM_ROM_DIR) + "/bbc-basic_2.rom");
        auto dfs = load_rom(std::string(BEEBIUM_ROM_DIR) + "/acorn-dfs_2_26.rom");
        std::copy(mos.begin(), mos.end(), machine_.state().memory.mos_rom.data());
        machine_.state().memory.load_sideways_rom(15, basic.data(), basic.size());
        machine_.state().memory.load_sideways_rom(11, dfs.data(), dfs.size());
#endif
        machine_.reset();
        // Apply the link state to the memory wiring so that, at the
        // dispatch level, IC71 only responds at the slots S13 selected.
        // Production code does this in load_roms(); the fixture replicates
        // that step explicitly because it doesn't go through load_roms.
        machine_.state().memory.apply_motherboard_links(links);
        server_ = std::make_unique<beebium::service::Server<beebium::ModelBPlus>>(
            machine_, "127.0.0.1", 0);
        // set_motherboard_links must be called BEFORE start() -- the
        // sideways service is constructed during start() and reads the
        // saved link state at that point.
        server_->set_motherboard_links(links);
        server_->start({}, {});

        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        sideways_stub_ = beebium::SidewaysService::NewStub(channel_);
    }

    ~ModelBPlusSidewaysFixture() { server_->stop(); }

    beebium::ModelBPlus& machine() { return machine_; }
    beebium::SidewaysService::Stub& sideways() { return *sideways_stub_; }

private:
    beebium::ModelBPlus machine_;
    std::unique_ptr<beebium::service::Server<beebium::ModelBPlus>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::SidewaysService::Stub> sideways_stub_;
};

} // anonymous namespace

//////////////////////////////////////////////////////////////////////////////
// Model B (Aliased Sideways) Tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("SidewaysService GetSlotStatus returns aliasing info for Model B", "[grpc][sideways]") {
    ModelBSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::GetSlotStatusRequest request;
    beebium::GetSlotStatusResponse response;

    auto status = fixture.sideways().GetSlotStatus(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.has_aliasing() == true);
    CHECK(response.num_physical_slots() == 4);
    CHECK(response.sockets_size() == 4);
}

TEST_CASE("SidewaysService GetSlotStatus reports correct aliased slots for Model B", "[grpc][sideways]") {
    ModelBSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::GetSlotStatusRequest request;
    beebium::GetSlotStatusResponse response;

    auto status = fixture.sideways().GetSlotStatus(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.sockets_size() == 4);

    // Socket 0 (IC52) should alias slots 0, 4, 8, 12
    const auto& socket0 = response.sockets(0);
    CHECK(socket0.socket_index() == 0);
    REQUIRE(socket0.aliased_slots_size() == 4);
    CHECK(socket0.aliased_slots(0) == 0);
    CHECK(socket0.aliased_slots(1) == 4);
    CHECK(socket0.aliased_slots(2) == 8);
    CHECK(socket0.aliased_slots(3) == 12);

    // Socket 3 (IC101) should alias slots 3, 7, 11, 15 (BASIC)
    const auto& socket3 = response.sockets(3);
    CHECK(socket3.socket_index() == 3);
    REQUIRE(socket3.aliased_slots_size() == 4);
    CHECK(socket3.aliased_slots(0) == 3);
    CHECK(socket3.aliased_slots(1) == 7);
    CHECK(socket3.aliased_slots(2) == 11);
    CHECK(socket3.aliased_slots(3) == 15);
}

TEST_CASE("SidewaysService GetSlotStatus reports BASIC socket is populated", "[grpc][sideways]") {
    ModelBSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::GetSlotStatusRequest request;
    beebium::GetSlotStatusResponse response;

    auto status = fixture.sideways().GetSlotStatus(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.sockets_size() == 4);

    // Socket 3 (IC101) holds BASIC and should be populated
    const auto& socket3 = response.sockets(3);
    CHECK(socket3.populated() == true);
    CHECK(socket3.type() == beebium::SIDEWAYS_SLOT_TYPE_ROM);
}

TEST_CASE("SidewaysService GetSlotStatus reports parsed ROM header for BASIC",
          "[grpc][sideways][rom_header]") {
    ModelBSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::GetSlotStatusRequest request;
    beebium::GetSlotStatusResponse response;
    auto status = fixture.sideways().GetSlotStatus(&context, request, &response);
    REQUIRE(status.ok());
    REQUIRE(response.sockets_size() == 4);

    // Socket 3 (IC101) holds BASIC; its rom_header should be recognised, with
    // title "BASIC", the language kind, and no ROMFS content.
    const auto& socket3 = response.sockets(3);
    REQUIRE(socket3.has_rom_header());
    const auto& header = socket3.rom_header();
    CHECK(header.recognised() == true);
    CHECK(header.title() == "BASIC");
    CHECK(header.copyright() == "(C)1982 Acorn");
    CHECK(header.contains_romfs() == false);
    REQUIRE(header.kinds_size() == 1);
    CHECK(header.kinds(0) == "language");

    // The other Model B sockets are empty (open bus) - their rom_header is
    // not populated.
    CHECK_FALSE(response.sockets(0).has_rom_header());
}

TEST_CASE("SidewaysService ConfigureSlot with invalid slot returns error", "[grpc][sideways]") {
    ModelBSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::ConfigureSlotRequest request;
    beebium::ConfigureSlotResponse response;

    request.set_slot(16);  // Invalid slot number
    request.set_type(beebium::SIDEWAYS_SLOT_TYPE_RAM);

    auto status = fixture.sideways().ConfigureSlot(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.success() == false);
    CHECK(response.error().find("Invalid slot") != std::string::npos);
}

TEST_CASE("SidewaysService ConfigureSlot rejects runtime configuration on Model B",
          "[grpc][sideways]") {
    // Model B sockets are not runtime-reconfigurable (real hardware would
    // need a power cycle to swap chips). Initial layout is set via
    // --sideways at startup; the runtime RPC must refuse.
    ModelBSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::ConfigureSlotRequest request;
    beebium::ConfigureSlotResponse response;

    request.set_slot(4);
    request.set_type(beebium::SIDEWAYS_SLOT_TYPE_RAM);

    auto status = fixture.sideways().ConfigureSlot(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.success() == false);
    CHECK(response.error().find("not runtime-reconfigurable") != std::string::npos);
    CHECK(response.error().find("IC52") != std::string::npos);  // socket label included
}

TEST_CASE("SidewaysService ConfigureSlot accepts runtime configuration on ROM/RAM board",
          "[grpc][sideways]") {
    // The ROM/RAM expansion board is the fantasy hardware that DOES support
    // runtime reconfiguration -- the topology says so, ConfigureSlot honours it.
    RomRamBoardSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::ConfigureSlotRequest request;
    beebium::ConfigureSlotResponse response;

    request.set_slot(4);
    request.set_type(beebium::SIDEWAYS_SLOT_TYPE_RAM);

    auto status = fixture.sideways().ConfigureSlot(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.success() == true);
    CHECK(response.actual_socket() == 4);  // 1:1 slot/socket on this board
}

TEST_CASE("SidewaysService ReadSlotData reads ROM contents", "[grpc][sideways]") {
    ModelBSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::ReadSlotDataRequest request;
    beebium::ReadSlotDataResponse response;

    // Read first 16 bytes from BASIC ROM at slot 15 (socket 3)
    request.set_slot(15);
    request.set_offset(0);
    request.set_length(16);

    auto status = fixture.sideways().ReadSlotData(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.success() == true);
    CHECK(response.data().size() == 16);
    CHECK(response.type() == beebium::SIDEWAYS_SLOT_TYPE_ROM);
}

TEST_CASE("SidewaysService ReadSlotData returns same content for aliased slots", "[grpc][sideways]") {
    ModelBSidewaysFixture fixture;

    // Read from slot 15 (BASIC)
    grpc::ClientContext context1;
    beebium::ReadSlotDataRequest request1;
    beebium::ReadSlotDataResponse response1;
    request1.set_slot(15);
    request1.set_offset(0);
    request1.set_length(16);
    auto status1 = fixture.sideways().ReadSlotData(&context1, request1, &response1);
    REQUIRE(status1.ok());

    // Read from slot 3 (also maps to IC101, same as slot 15)
    grpc::ClientContext context2;
    beebium::ReadSlotDataRequest request2;
    beebium::ReadSlotDataResponse response2;
    request2.set_slot(3);
    request2.set_offset(0);
    request2.set_length(16);
    auto status2 = fixture.sideways().ReadSlotData(&context2, request2, &response2);
    REQUIRE(status2.ok());

    // Both should return identical data due to aliasing
    CHECK(response1.data() == response2.data());
}

TEST_CASE("SidewaysService ReadSlotData with invalid slot returns error", "[grpc][sideways]") {
    ModelBSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::ReadSlotDataRequest request;
    beebium::ReadSlotDataResponse response;

    request.set_slot(16);  // Invalid
    request.set_offset(0);
    request.set_length(16);

    auto status = fixture.sideways().ReadSlotData(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.success() == false);
    CHECK(response.error().find("Invalid slot") != std::string::npos);
}

TEST_CASE("SidewaysService ReadSlotData clamps length to slot boundary", "[grpc][sideways]") {
    ModelBSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::ReadSlotDataRequest request;
    beebium::ReadSlotDataResponse response;

    // Request extends past 16KB boundary
    request.set_slot(15);
    request.set_offset(16380);
    request.set_length(100);

    auto status = fixture.sideways().ReadSlotData(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.success() == true);
    // Should be clamped to 4 bytes (16384 - 16380)
    CHECK(response.data().size() == 4);
}

//////////////////////////////////////////////////////////////////////////////
// ROM/RAM Board (Non-Aliased Sideways) Tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("SidewaysService GetSlotStatus returns 16 slots for ROM/RAM board", "[grpc][sideways]") {
    RomRamBoardSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::GetSlotStatusRequest request;
    beebium::GetSlotStatusResponse response;

    auto status = fixture.sideways().GetSlotStatus(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.has_aliasing() == false);
    CHECK(response.num_physical_slots() == 16);
    CHECK(response.sockets_size() == 16);
}

TEST_CASE("SidewaysService GetSlotStatus reports no aliasing for ROM/RAM board", "[grpc][sideways]") {
    RomRamBoardSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::GetSlotStatusRequest request;
    beebium::GetSlotStatusResponse response;

    auto status = fixture.sideways().GetSlotStatus(&context, request, &response);

    REQUIRE(status.ok());

    // Each slot should map only to itself
    for (int i = 0; i < 16; ++i) {
        const auto& slot = response.sockets(i);
        CHECK(slot.socket_index() == static_cast<uint32_t>(i));
        REQUIRE(slot.aliased_slots_size() == 1);
        CHECK(slot.aliased_slots(0) == static_cast<uint32_t>(i));
    }
}

TEST_CASE("SidewaysService ConfigureSlot actual_socket equals slot for ROM/RAM board", "[grpc][sideways]") {
    RomRamBoardSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::ConfigureSlotRequest request;
    beebium::ConfigureSlotResponse response;

    request.set_slot(7);
    request.set_type(beebium::SIDEWAYS_SLOT_TYPE_RAM);

    auto status = fixture.sideways().ConfigureSlot(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.success() == true);
    CHECK(response.actual_socket() == 7);  // No aliasing, socket = slot
}

TEST_CASE("SidewaysService ConfigureSlot with data loads image", "[grpc][sideways]") {
    RomRamBoardSidewaysFixture fixture;

    // Configure slot 5 as ROM with test data
    grpc::ClientContext configure_context;
    beebium::ConfigureSlotRequest configure_request;
    beebium::ConfigureSlotResponse configure_response;

    configure_request.set_slot(5);
    configure_request.set_type(beebium::SIDEWAYS_SLOT_TYPE_ROM);

    // Create test data pattern
    std::string test_data(256, '\0');
    for (int i = 0; i < 256; ++i) {
        test_data[i] = static_cast<char>(i);
    }
    configure_request.set_data(test_data);

    auto configure_status = fixture.sideways().ConfigureSlot(
        &configure_context, configure_request, &configure_response);

    REQUIRE(configure_status.ok());
    REQUIRE(configure_response.success() == true);

    // Read back the data
    grpc::ClientContext read_context;
    beebium::ReadSlotDataRequest read_request;
    beebium::ReadSlotDataResponse read_response;

    read_request.set_slot(5);
    read_request.set_offset(0);
    read_request.set_length(256);

    auto read_status = fixture.sideways().ReadSlotData(
        &read_context, read_request, &read_response);

    REQUIRE(read_status.ok());
    REQUIRE(read_response.success() == true);
    CHECK(read_response.data() == test_data);
}

TEST_CASE("SidewaysService empty slot returns 0xFF", "[grpc][sideways]") {
    RomRamBoardSidewaysFixture fixture;

    // Configure slot 10 as empty
    grpc::ClientContext configure_context;
    beebium::ConfigureSlotRequest configure_request;
    beebium::ConfigureSlotResponse configure_response;

    configure_request.set_slot(10);
    configure_request.set_type(beebium::SIDEWAYS_SLOT_TYPE_EMPTY);

    auto configure_status = fixture.sideways().ConfigureSlot(
        &configure_context, configure_request, &configure_response);

    REQUIRE(configure_status.ok());
    REQUIRE(configure_response.success() == true);

    // Read from empty slot should return 0xFF
    grpc::ClientContext read_context;
    beebium::ReadSlotDataRequest read_request;
    beebium::ReadSlotDataResponse read_response;

    read_request.set_slot(10);
    read_request.set_offset(0);
    read_request.set_length(16);

    auto read_status = fixture.sideways().ReadSlotData(
        &read_context, read_request, &read_response);

    REQUIRE(read_status.ok());
    REQUIRE(read_response.success() == true);
    CHECK(read_response.data().size() == 16);
    CHECK(read_response.type() == beebium::SIDEWAYS_SLOT_TYPE_EMPTY);

    // All bytes should be 0xFF
    for (char c : read_response.data()) {
        CHECK(static_cast<uint8_t>(c) == 0xFF);
    }
}

//////////////////////////////////////////////////////////////////////////////
// DebuggerControl PeekRegion Tests (ROM/RAM Board)
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("DebuggerControl PeekRegion returns BASIC ROM data for bank_15", "[grpc][debugger][peek_region]") {
    RomRamBoardSidewaysFixture fixture;

    // BASIC ROM was loaded in fixture constructor via load_basic()
    // PeekRegion for "bank_15" at address 0x8000 should return BASIC ROM data

    grpc::ClientContext context;
    beebium::RegionAccessRequest request;
    beebium::RegionAccessResponse response;

    request.set_region_name("bank_15");
    request.set_address(0x8000);
    request.set_length(16);

    auto status = fixture.debugger().PeekRegion(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.data().size() == 16);

    // BBC BASIC II starts with 0xC9 (CMP immediate opcode)
    // This is the same check used in the oracle tests
    CHECK(static_cast<uint8_t>(response.data()[0]) == 0xC9);
}

TEST_CASE("DebuggerControl PeekRegion returns DFS ROM data for bank_13", "[grpc][debugger][peek_region]") {
    RomRamBoardSidewaysFixture fixture;

    // The fixture loads BASIC but doesn't load DFS by default
    // Let's load DFS into slot 13 first
#ifdef BEEBIUM_ROM_DIR
    auto dfs = load_rom(std::string(BEEBIUM_ROM_DIR) + "/acorn-dfs_2_26.rom");
    fixture.machine().state().memory.load_sideways_rom(13, dfs.data(), dfs.size());
#endif

    grpc::ClientContext context;
    beebium::RegionAccessRequest request;
    beebium::RegionAccessResponse response;

    request.set_region_name("bank_13");
    request.set_address(0x8000);
    request.set_length(16);

    auto status = fixture.debugger().PeekRegion(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.data().size() == 16);

    // DFS 2.26 ROM header has specific signature bytes
    // If DFS loaded correctly, first byte should not be 0xFF
    CHECK(static_cast<uint8_t>(response.data()[0]) != 0xFF);
}

TEST_CASE("DebuggerControl PeekRegion returns empty slot data for unconfigured bank", "[grpc][debugger][peek_region]") {
    RomRamBoardSidewaysFixture fixture;

    // Slot 0 should be empty by default in the fixture (only BASIC is loaded to slot 15)
    grpc::ClientContext context;
    beebium::RegionAccessRequest request;
    beebium::RegionAccessResponse response;

    request.set_region_name("bank_0");
    request.set_address(0x8000);
    request.set_length(16);

    auto status = fixture.debugger().PeekRegion(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.data().size() == 16);

    // Empty slot should return 0xFF for all bytes
    for (char c : response.data()) {
        CHECK(static_cast<uint8_t>(c) == 0xFF);
    }
}

TEST_CASE("DebuggerControl PeekRegion returns error for invalid region name", "[grpc][debugger][peek_region][validation]") {
    RomRamBoardSidewaysFixture fixture;

    SECTION("unknown region name returns INVALID_ARGUMENT") {
        grpc::ClientContext context;
        beebium::RegionAccessRequest request;
        beebium::RegionAccessResponse response;

        request.set_region_name("typo_in_region_name");
        request.set_address(0x8000);
        request.set_length(16);

        auto status = fixture.debugger().PeekRegion(&context, request, &response);

        REQUIRE(!status.ok());
        CHECK(status.error_code() == grpc::StatusCode::INVALID_ARGUMENT);
        CHECK(status.error_message().find("unknown region") != std::string::npos);
    }

    SECTION("empty region name returns INVALID_ARGUMENT") {
        grpc::ClientContext context;
        beebium::RegionAccessRequest request;
        beebium::RegionAccessResponse response;

        // Don't set region_name - it will be empty
        request.set_address(0x8000);
        request.set_length(16);

        auto status = fixture.debugger().PeekRegion(&context, request, &response);

        REQUIRE(!status.ok());
        CHECK(status.error_code() == grpc::StatusCode::INVALID_ARGUMENT);
        // Error message shows empty string was passed: "unknown region: ''"
        CHECK(status.error_message().find("unknown region") != std::string::npos);
    }

    SECTION("bank_16 (out of range) returns INVALID_ARGUMENT") {
        grpc::ClientContext context;
        beebium::RegionAccessRequest request;
        beebium::RegionAccessResponse response;

        request.set_region_name("bank_16");
        request.set_address(0x8000);
        request.set_length(16);

        auto status = fixture.debugger().PeekRegion(&context, request, &response);

        REQUIRE(!status.ok());
        CHECK(status.error_code() == grpc::StatusCode::INVALID_ARGUMENT);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Socket capability fields populated from SlotTopology
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("SidewaysService GetSlotStatus reports Model B socket capabilities",
          "[grpc][sideways][capabilities]") {
    ModelBSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::GetSlotStatusRequest request;
    beebium::GetSlotStatusResponse response;

    auto status = fixture.sideways().GetSlotStatus(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.sockets_size() == 4);

    // Model B sockets can hold any of ROM/RAM/Empty -- but only as
    // configured at start-up. They are NOT runtime-reconfigurable; the
    // capability flag must report this honestly so GUI clients don't
    // expose a useless "convert to RAM" command.
    for (int i = 0; i < response.sockets_size(); ++i) {
        const auto& caps = response.sockets(i).capabilities();
        CHECK(caps.supports_rom());
        CHECK(caps.supports_ram());
        CHECK(caps.supports_empty());
        CHECK_FALSE(caps.runtime_configurable());
    }
}

TEST_CASE("SidewaysService GetSlotStatus reports Model B+ topology including S13",
          "[grpc][sideways][model_b_plus][links]") {
    SECTION("default S13=South binds IC71 to slots 14/15") {
        ModelBPlusSidewaysFixture fixture;  // default links = S13 South

        grpc::ClientContext context;
        beebium::GetSlotStatusRequest request;
        beebium::GetSlotStatusResponse response;

        auto status = fixture.sideways().GetSlotStatus(&context, request, &response);

        REQUIRE(status.ok());
        CHECK(response.has_aliasing() == true);
        // 1 IC71 entry (aliased BASIC pair) + 10 independent user-socket
        // slots (IC35/44/57/62/68 split low/high) = 11.
        REQUIRE(response.sockets_size() == 11);
        REQUIRE(response.motherboard_links_size() == 1);
        CHECK(response.motherboard_links(0).name() == "S13");
        CHECK(response.motherboard_links(0).value() == "south");

        // IC71 should be the first socket and own slots 14, 15.
        const auto& ic71 = response.sockets(0);
        CHECK(ic71.socket_label() == "IC71");
        REQUIRE(ic71.aliased_slots_size() == 2);
        CHECK(ic71.aliased_slots(0) == 14);
        CHECK(ic71.aliased_slots(1) == 15);

        // IC71 (socket 0) is the soldered MOS+BASIC system ROM: ROM only,
        // not removable. The user sockets (IC35..IC68) support the full
        // ROM/RAM/empty trifecta. Nothing on the B+ is runtime-reconfigurable.
        for (int i = 0; i < response.sockets_size(); ++i) {
            const auto& caps = response.sockets(i).capabilities();
            CHECK(caps.supports_rom());
            CHECK_FALSE(caps.runtime_configurable());
            if (i == 0) {
                CHECK_FALSE(caps.supports_ram());
                CHECK_FALSE(caps.supports_empty());
            } else {
                CHECK(caps.supports_ram());
                CHECK(caps.supports_empty());
            }
        }
    }

    SECTION("S13=North rebinds IC71 to slots 0/1") {
        beebium::ModelBPlusHardware::MotherboardLinks links;
        links.s13 = beebium::ModelBPlusHardware::MotherboardLinks::S13Position::North;
        ModelBPlusSidewaysFixture fixture{links};

        grpc::ClientContext context;
        beebium::GetSlotStatusRequest request;
        beebium::GetSlotStatusResponse response;

        auto status = fixture.sideways().GetSlotStatus(&context, request, &response);

        REQUIRE(status.ok());
        REQUIRE(response.motherboard_links_size() == 1);
        CHECK(response.motherboard_links(0).value() == "north");

        const auto& ic71 = response.sockets(0);
        CHECK(ic71.socket_label() == "IC71");
        REQUIRE(ic71.aliased_slots_size() == 2);
        CHECK(ic71.aliased_slots(0) == 0);
        CHECK(ic71.aliased_slots(1) == 1);
    }

}

TEST_CASE("SidewaysService GetSlotStatus reports ROM/RAM board socket capabilities",
          "[grpc][sideways][capabilities]") {
    // The ROM/RAM expansion board is the variant where runtime
    // reconfiguration is allowed.
    RomRamBoardSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::GetSlotStatusRequest request;
    beebium::GetSlotStatusResponse response;

    auto status = fixture.sideways().GetSlotStatus(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.sockets_size() == 16);

    for (int i = 0; i < response.sockets_size(); ++i) {
        const auto& caps = response.sockets(i).capabilities();
        CHECK(caps.supports_rom());
        CHECK(caps.supports_ram());
        CHECK(caps.supports_empty());
        CHECK(caps.runtime_configurable());
    }
}

// Regression: the sidebar showed every Model B+ socket as empty even when
// BASIC and DFS were loaded (the integral B+ configuration). GetSlotStatus
// must report the loaded ROMs with their parsed headers.
TEST_CASE("SidewaysService GetSlotStatus reports populated Model B+ ROMs",
          "[grpc][sideways][model_b_plus][rom_header]") {
    ModelBPlusSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::GetSlotStatusRequest request;
    beebium::GetSlotStatusResponse response;
    auto status = fixture.sideways().GetSlotStatus(&context, request, &response);

    REQUIRE(status.ok());
    // 1 IC71 + 10 user-socket slots (5 sockets x low/high) = 11.
    REQUIRE(response.sockets_size() == 11);

    // Build a label-keyed view because iteration order is topology-defined.
    auto find_socket = [&](const std::string& label)
        -> const beebium::SocketStatus* {
        for (int i = 0; i < response.sockets_size(); ++i) {
            if (response.sockets(i).socket_label() == label) {
                return &response.sockets(i);
            }
        }
        return nullptr;
    };

    SECTION("IC71 reports the loaded BASIC ROM") {
        const auto* ic71 = find_socket("IC71");
        REQUIRE(ic71 != nullptr);
        CHECK(ic71->type() == beebium::SIDEWAYS_SLOT_TYPE_ROM);
        CHECK(ic71->populated());
        REQUIRE(ic71->has_rom_header());
        const auto& h = ic71->rom_header();
        CHECK(h.recognised());
        CHECK(h.title() == "BASIC");
        CHECK(h.copyright() == "(C)1982 Acorn");
    }

    SECTION("IC68 slot 11 reports the loaded DFS ROM") {
        const auto* ic68_hi = find_socket("IC68 (slot 11)");
        REQUIRE(ic68_hi != nullptr);
        CHECK(ic68_hi->type() == beebium::SIDEWAYS_SLOT_TYPE_ROM);
        CHECK(ic68_hi->populated());
        REQUIRE(ic68_hi->has_rom_header());
        const auto& h = ic68_hi->rom_header();
        CHECK(h.recognised());
        CHECK(h.title() == "DFS");
    }

    SECTION("IC68 slot 10 is empty (DFS is at slot 11, low half unloaded)") {
        const auto* ic68_lo = find_socket("IC68 (slot 10)");
        REQUIRE(ic68_lo != nullptr);
        CHECK(ic68_lo->type() == beebium::SIDEWAYS_SLOT_TYPE_EMPTY);
        CHECK_FALSE(ic68_lo->populated());
    }

    SECTION("Empty user socket halves report no ROM") {
        const char* empty_labels[] = {
            "IC35 (slot 2)", "IC35 (slot 3)",
            "IC44 (slot 4)", "IC44 (slot 5)",
            "IC57 (slot 6)", "IC57 (slot 7)",
            "IC62 (slot 8)", "IC62 (slot 9)",
            "IC68 (slot 10)",
        };
        for (const char* label : empty_labels) {
            const auto* sock = find_socket(label);
            REQUIRE(sock != nullptr);
            INFO("socket label: " << label);
            CHECK(sock->type() == beebium::SIDEWAYS_SLOT_TYPE_EMPTY);
            CHECK_FALSE(sock->populated());
            CHECK_FALSE(sock->has_rom_header());
        }
    }
}

//////////////////////////////////////////////////////////////////////////////
// Model B+ 128K Tests - integral sideways RAM at W/X/Y/Z per AN 030
//////////////////////////////////////////////////////////////////////////////

namespace {
const beebium::SocketStatus* find_socket_by_label(
    const beebium::GetSlotStatusResponse& response, const std::string& label)
{
    for (int i = 0; i < response.sockets_size(); ++i) {
        if (response.sockets(i).socket_label() == label) {
            return &response.sockets(i);
        }
    }
    return nullptr;
}
} // namespace

TEST_CASE("SidewaysService GetSlotStatus reports B+ 128K topology, S13=South",
          "[grpc][sideways][model_b_plus_128k]") {
    ModelBPlus128KSidewaysFixture fixture;  // default S13=South

    grpc::ClientContext context;
    beebium::GetSlotStatusRequest request;
    beebium::GetSlotStatusResponse response;
    auto status = fixture.sideways().GetSlotStatus(&context, request, &response);
    REQUIRE(status.ok());
    // 1 IC71 (aliased) + 10 user-socket slots (5 sockets x low/high)
    //   + 4 SRAM banks (W/X/Y/Z) = 15 entries.
    REQUIRE(response.sockets_size() == 15);

    SECTION("IC71 is aliased; user sockets are split into low/high slots") {
        const auto* ic71 = find_socket_by_label(response, "IC71");
        REQUIRE(ic71 != nullptr);
        REQUIRE(ic71->aliased_slots_size() == 2);
        CHECK(ic71->aliased_slots(0) == 14);
        CHECK(ic71->aliased_slots(1) == 15);

        // IC68 is now two distinct rows for slots 10 and 11.
        const auto* ic68_lo = find_socket_by_label(response, "IC68 (slot 10)");
        REQUIRE(ic68_lo != nullptr);
        REQUIRE(ic68_lo->aliased_slots_size() == 1);
        CHECK(ic68_lo->aliased_slots(0) == 10);

        const auto* ic68_hi = find_socket_by_label(response, "IC68 (slot 11)");
        REQUIRE(ic68_hi != nullptr);
        REQUIRE(ic68_hi->aliased_slots_size() == 1);
        CHECK(ic68_hi->aliased_slots(0) == 11);
    }

    SECTION("SRAM W is always at slot 12") {
        const auto* w = find_socket_by_label(response, "SRAM W");
        REQUIRE(w != nullptr);
        REQUIRE(w->aliased_slots_size() == 1);
        CHECK(w->aliased_slots(0) == 12);
        CHECK(w->type() == beebium::SIDEWAYS_SLOT_TYPE_RAM);
        const auto& caps = w->capabilities();
        CHECK_FALSE(caps.supports_rom());
        CHECK(caps.supports_ram());
        CHECK_FALSE(caps.supports_empty());
        CHECK_FALSE(caps.runtime_configurable());
    }

    SECTION("SRAM X is always at slot 13") {
        const auto* x = find_socket_by_label(response, "SRAM X");
        REQUIRE(x != nullptr);
        REQUIRE(x->aliased_slots_size() == 1);
        CHECK(x->aliased_slots(0) == 13);
        CHECK(x->type() == beebium::SIDEWAYS_SLOT_TYPE_RAM);
    }

    SECTION("SRAM Y is at slot 0 when S13=South") {
        const auto* y = find_socket_by_label(response, "SRAM Y");
        REQUIRE(y != nullptr);
        REQUIRE(y->aliased_slots_size() == 1);
        CHECK(y->aliased_slots(0) == 0);
        CHECK(y->type() == beebium::SIDEWAYS_SLOT_TYPE_RAM);
    }

    SECTION("SRAM Z is at slot 1 when S13=South") {
        const auto* z = find_socket_by_label(response, "SRAM Z");
        REQUIRE(z != nullptr);
        REQUIRE(z->aliased_slots_size() == 1);
        CHECK(z->aliased_slots(0) == 1);
        CHECK(z->type() == beebium::SIDEWAYS_SLOT_TYPE_RAM);
    }
}

TEST_CASE("SidewaysService GetSlotStatus reports B+ 128K topology, S13=North",
          "[grpc][sideways][model_b_plus_128k]") {
    beebium::ModelBPlus128KHardware::MotherboardLinks links;
    links.s13 = beebium::ModelBPlus128KHardware
                    ::MotherboardLinks::S13Position::North;
    ModelBPlus128KSidewaysFixture fixture{links};

    grpc::ClientContext context;
    beebium::GetSlotStatusRequest request;
    beebium::GetSlotStatusResponse response;
    auto status = fixture.sideways().GetSlotStatus(&context, request, &response);
    REQUIRE(status.ok());

    SECTION("IC71 (BASIC) moves to slots 0, 1") {
        const auto* ic71 = find_socket_by_label(response, "IC71");
        REQUIRE(ic71 != nullptr);
        REQUIRE(ic71->aliased_slots_size() == 2);
        CHECK(ic71->aliased_slots(0) == 0);
        CHECK(ic71->aliased_slots(1) == 1);
    }

    SECTION("SRAM Y/Z move opposite IC71 to slots 14, 15") {
        const auto* y = find_socket_by_label(response, "SRAM Y");
        REQUIRE(y != nullptr);
        REQUIRE(y->aliased_slots_size() == 1);
        CHECK(y->aliased_slots(0) == 14);

        const auto* z = find_socket_by_label(response, "SRAM Z");
        REQUIRE(z != nullptr);
        REQUIRE(z->aliased_slots_size() == 1);
        CHECK(z->aliased_slots(0) == 15);
    }

    SECTION("SRAM W/X stay at slots 12, 13 regardless of S13") {
        const auto* w = find_socket_by_label(response, "SRAM W");
        REQUIRE(w != nullptr);
        CHECK(w->aliased_slots(0) == 12);
        const auto* x = find_socket_by_label(response, "SRAM X");
        REQUIRE(x != nullptr);
        CHECK(x->aliased_slots(0) == 13);
    }
}

TEST_CASE("B+ 128K SRAM banks accept and return bytes (smoke test)",
          "[grpc][sideways][model_b_plus_128k]") {
    ModelBPlus128KSidewaysFixture fixture;  // default S13=South

    // Write a sentinel byte to each RAM bank via the CPU's bank-write
    // path, then read it back through ReadSlotData. This proves the
    // four banks are wired to the right slots and are read/writable.
    auto& memory = fixture.machine().state().memory;
    memory.sideways.write_bank(12, 0x0000, 0xAA);  // W
    memory.sideways.write_bank(13, 0x1234, 0xBB);  // X
    memory.sideways.write_bank(0,  0x2345, 0xCC);  // Y
    memory.sideways.write_bank(1,  0x3456, 0xDD);  // Z

    auto read_byte = [&](uint8_t slot, uint16_t offset) -> uint8_t {
        grpc::ClientContext context;
        beebium::ReadSlotDataRequest request;
        beebium::ReadSlotDataResponse response;
        request.set_slot(slot);
        request.set_offset(offset);
        request.set_length(1);
        auto status = fixture.sideways().ReadSlotData(&context, request, &response);
        REQUIRE(status.ok());
        REQUIRE(response.success());
        REQUIRE(response.data().size() == 1);
        return static_cast<uint8_t>(response.data()[0]);
    };

    CHECK(read_byte(12, 0x0000) == 0xAA);
    CHECK(read_byte(13, 0x1234) == 0xBB);
    CHECK(read_byte(0,  0x2345) == 0xCC);
    CHECK(read_byte(1,  0x3456) == 0xDD);
}

// Regression: when the user pre-loaded a ROM into slot 0 of a B+ 128K
// (SRAM Y with S13=South), load_sideways_rom routed the bytes to
// basic_rom rather than sram_y, silently overwriting BASIC. The
// machine then hung on boot with "Language?" because the MOS couldn't
// find a language ROM. load_sideways_rom must consult S13 to pick the
// right destination for the slots IC71 and SRAM Y/Z share.
TEST_CASE("B+ 128K load_sideways_rom routes slots 0/1/14/15 by S13",
          "[sideways][model_b_plus_128k][regression]") {
    using Links = beebium::ModelBPlus128KHardware::MotherboardLinks;

    auto fill = [](uint8_t byte) {
        std::vector<uint8_t> data(16384, byte);
        return data;
    };

    SECTION("S13=South: slots 0/1 are SRAM Y/Z, slots 14/15 are BASIC") {
        beebium::ModelBPlus128K machine;
        machine.reset();
        machine.state().memory.apply_motherboard_links(Links{});  // South

        auto basic = fill(0xBA);
        auto y = fill(0x5A);
        auto z = fill(0x5B);

        // load_sideways_rom is the ROM-load path: writes to basic_rom
        // for the S13-active BASIC pair, no-op for fixed-RAM slots.
        // load_sideways_data is the RAM preload path: writes to the
        // SRAM banks. This split matches what the server's
        // apply_sideways_configs does per --sideways grammar.
        machine.state().memory.load_sideways_rom(
            15, basic.data(), basic.size());   // BASIC at 14/15
        machine.state().memory.load_sideways_data(
            0, y.data(), y.size());            // SRAM Y at slot 0
        machine.state().memory.load_sideways_data(
            1, z.data(), z.size());            // SRAM Z at slot 1

        // Verify each underlying device received the right bytes.
        CHECK(machine.state().memory.basic_rom.read(0) == 0xBA);
        CHECK(machine.state().memory.sram_y.read(0)   == 0x5A);
        CHECK(machine.state().memory.sram_z.read(0)   == 0x5B);

        // The most important assertion: SRAM Y's load didn't overwrite
        // BASIC (the original bug).
        CHECK(machine.state().memory.basic_rom.read(0) == 0xBA);
    }

    SECTION("S13=North: slots 0/1 are BASIC, slots 14/15 are SRAM Y/Z") {
        beebium::ModelBPlus128K machine;
        machine.reset();
        Links links;
        links.s13 = Links::S13Position::North;
        machine.state().memory.apply_motherboard_links(links);

        auto basic = fill(0xBA);
        auto y = fill(0x5A);
        auto z = fill(0x5B);

        machine.state().memory.load_sideways_rom(
            0, basic.data(), basic.size());    // BASIC at 0/1
        machine.state().memory.load_sideways_data(
            14, y.data(), y.size());           // SRAM Y at slot 14
        machine.state().memory.load_sideways_data(
            15, z.data(), z.size());           // SRAM Z at slot 15

        CHECK(machine.state().memory.basic_rom.read(0) == 0xBA);
        CHECK(machine.state().memory.sram_y.read(0)   == 0x5A);
        CHECK(machine.state().memory.sram_z.read(0)   == 0x5B);
    }
}

//////////////////////////////////////////////////////////////////////////////
// ATPL Sidewise Tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("SidewaysService GetSlotStatus reports 16 fixed slots for ATPL Sidewise",
          "[grpc][sideways][atpl_sidewise]") {
    AtplSidewiseSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::GetSlotStatusRequest request;
    beebium::GetSlotStatusResponse response;
    auto status = fixture.sideways().GetSlotStatus(&context, request, &response);

    REQUIRE(status.ok());
    CHECK_FALSE(response.has_aliasing());
    REQUIRE(response.sockets_size() == 16);

    for (int i = 0; i < response.sockets_size(); ++i) {
        const auto& socket = response.sockets(i);
        const auto& caps = socket.capabilities();
        // No slot is runtime-reconfigurable on a real board.
        CHECK_FALSE(caps.runtime_configurable());
        CHECK(caps.supports_rom());
        // Only slot 15 can hold RAM (and carries the write-protect group).
        if (socket.socket_index() == 15) {
            CHECK(caps.supports_ram());
        } else {
            CHECK_FALSE(caps.supports_ram());
        }
    }
}

TEST_CASE("SidewaysService reports no protection groups on B+ 128K SRAM",
          "[grpc][sideways][model_b_plus][protection]") {
    // The B+ 128K has sideways RAM (SRAM W/X/Y/Z) but no protection switch, so it
    // reports no protection groups and SetSlotProtection is unavailable - a
    // front-end offers no protection control for it.
    ModelBPlus128KSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::GetSlotStatusRequest request;
    beebium::GetSlotStatusResponse response;
    REQUIRE(fixture.sideways().GetSlotStatus(&context, request, &response).ok());
    CHECK(response.protection_groups_size() == 0);

    bool saw_ram = false;
    for (int i = 0; i < response.sockets_size(); ++i) {
        if (response.sockets(i).type() == beebium::SIDEWAYS_SLOT_TYPE_RAM) {
            saw_ram = true;
        }
    }
    CHECK(saw_ram);  // the fixture must actually expose SRAM to make the point

    grpc::ClientContext ctx;
    beebium::SetSlotProtectionRequest req;
    beebium::SetSlotProtectionResponse resp;
    req.set_group_id("slot-12");
    req.set_kind(beebium::SIDEWAYS_PROTECTION_KIND_WRITE_PROTECT);
    req.set_engaged(true);
    REQUIRE(fixture.sideways().SetSlotProtection(&ctx, req, &resp).ok());
    CHECK_FALSE(resp.success());
}

TEST_CASE("SidewaysService ConfigureSlot is rejected on ATPL Sidewise",
          "[grpc][sideways][atpl_sidewise]") {
    AtplSidewiseSidewaysFixture fixture;

    grpc::ClientContext context;
    beebium::ConfigureSlotRequest request;
    beebium::ConfigureSlotResponse response;
    request.set_slot(15);
    request.set_type(beebium::SIDEWAYS_SLOT_TYPE_ROM);

    auto status = fixture.sideways().ConfigureSlot(&context, request, &response);

    REQUIRE(status.ok());
    CHECK_FALSE(response.success());  // fixed at launch, not runtime-configurable
}

TEST_CASE("SidewaysService SetSlotProtection toggles the ATPL slot-15 write group",
          "[grpc][sideways][atpl_sidewise][protection]") {
    AtplSidewiseSidewaysFixture fixture;

    // The group is enumerated for a front-end to build a control from.
    auto slot15_group = [&]() {
        grpc::ClientContext ctx;
        beebium::GetSlotStatusRequest req;
        beebium::GetSlotStatusResponse resp;
        REQUIRE(fixture.sideways().GetSlotStatus(&ctx, req, &resp).ok());
        REQUIRE(resp.protection_groups_size() == 1);
        return resp.protection_groups(0);
    };

    auto g = slot15_group();
    CHECK(g.id() == "slot-15");
    CHECK(g.supports_write_protect());
    CHECK_FALSE(g.supports_hide());
    CHECK(g.slots_size() == 1);
    CHECK(g.slots(0) == 15);
    CHECK_FALSE(g.write_protected());

    {
        grpc::ClientContext ctx;
        beebium::SetSlotProtectionRequest req;
        beebium::SetSlotProtectionResponse resp;
        req.set_group_id("slot-15");
        req.set_kind(beebium::SIDEWAYS_PROTECTION_KIND_WRITE_PROTECT);
        req.set_engaged(true);
        auto status = fixture.sideways().SetSlotProtection(&ctx, req, &resp);
        REQUIRE(status.ok());
        CHECK(resp.success());
        CHECK(resp.engaged());
    }
    CHECK(slot15_group().write_protected());

    {
        grpc::ClientContext ctx;
        beebium::SetSlotProtectionRequest req;
        beebium::SetSlotProtectionResponse resp;
        req.set_group_id("slot-15");
        req.set_kind(beebium::SIDEWAYS_PROTECTION_KIND_WRITE_PROTECT);
        req.set_engaged(false);
        REQUIRE(fixture.sideways().SetSlotProtection(&ctx, req, &resp).ok());
        CHECK(resp.success());
        CHECK_FALSE(resp.engaged());
    }
    CHECK_FALSE(slot15_group().write_protected());
}

TEST_CASE("SidewaysService SetSlotProtection rejects an unknown group or kind",
          "[grpc][sideways][atpl_sidewise][protection]") {
    AtplSidewiseSidewaysFixture fixture;

    // ATPL has only a slot-15 WRITE group: a read kind, or an unknown group, is
    // rejected.
    grpc::ClientContext ctx;
    beebium::SetSlotProtectionRequest req;
    beebium::SetSlotProtectionResponse resp;
    req.set_group_id("slot-15");
    req.set_kind(beebium::SIDEWAYS_PROTECTION_KIND_HIDE);
    req.set_engaged(true);
    REQUIRE(fixture.sideways().SetSlotProtection(&ctx, req, &resp).ok());
    CHECK_FALSE(resp.success());

    grpc::ClientContext ctx2;
    beebium::SetSlotProtectionResponse resp2;
    req.set_group_id("no-such-group");
    req.set_kind(beebium::SIDEWAYS_PROTECTION_KIND_WRITE_PROTECT);
    REQUIRE(fixture.sideways().SetSlotProtection(&ctx2, req, &resp2).ok());
    CHECK_FALSE(resp2.success());
}

TEST_CASE("SidewaysService SetSlotProtection is unavailable on Model B",
          "[grpc][sideways][protection]") {
    // The stock Model B has no protection controls; the RPC reports the feature
    // as unavailable rather than pretending to succeed.
    ModelBSidewaysFixture fixture;

    grpc::ClientContext ctx;
    beebium::SetSlotProtectionRequest req;
    beebium::SetSlotProtectionResponse resp;
    req.set_group_id("slot-0");
    req.set_kind(beebium::SIDEWAYS_PROTECTION_KIND_WRITE_PROTECT);
    req.set_engaged(true);

    REQUIRE(fixture.sideways().SetSlotProtection(&ctx, req, &resp).ok());
    CHECK_FALSE(resp.success());
}

TEST_CASE("SidewaysService reports Watford protection groups and write-select latch",
          "[grpc][sideways][watford][protection]") {
    WatfordRomRamSidewaysFixture fixture;

    grpc::ClientContext ctx;
    beebium::GetSlotStatusRequest req;
    beebium::GetSlotStatusResponse resp;
    REQUIRE(fixture.sideways().GetSlotStatus(&ctx, req, &resp).ok());

    REQUIRE(resp.protection_groups_size() == 2);
    const auto& s2 = resp.protection_groups(0);
    CHECK(s2.id() == "board");
    CHECK(s2.supports_write_protect());
    CHECK(s2.slots_size() == 16);
    const auto& s1 = resp.protection_groups(1);
    CHECK(s1.id() == "slot-14");
    CHECK(s1.supports_hide());
    REQUIRE(s1.slots_size() == 1);
    CHECK(s1.slots(0) == 14);

    // The &FF30 write-select latch is reported (read-only).
    CHECK(resp.write_select_latch().present());
    CHECK(resp.write_select_latch().socket() == 0);
}

TEST_CASE("SidewaysService SetSlotProtection drives the Watford S1/S2 switches",
          "[grpc][sideways][watford][protection]") {
    WatfordRomRamSidewaysFixture fixture;

    {
        grpc::ClientContext ctx;
        beebium::SetSlotProtectionRequest req;
        beebium::SetSlotProtectionResponse resp;
        req.set_group_id("board");
        req.set_kind(beebium::SIDEWAYS_PROTECTION_KIND_WRITE_PROTECT);
        req.set_engaged(true);
        REQUIRE(fixture.sideways().SetSlotProtection(&ctx, req, &resp).ok());
        CHECK(resp.success());
        CHECK(fixture.machine().state().memory.global_write_protect());
    }
    {
        grpc::ClientContext ctx;
        beebium::SetSlotProtectionRequest req;
        beebium::SetSlotProtectionResponse resp;
        req.set_group_id("slot-14");
        req.set_kind(beebium::SIDEWAYS_PROTECTION_KIND_HIDE);
        req.set_engaged(true);
        REQUIRE(fixture.sideways().SetSlotProtection(&ctx, req, &resp).ok());
        CHECK(resp.success());
        CHECK(fixture.machine().state().memory.bank14_read_protect());
    }
    // Read on the board group (write-only) is rejected.
    grpc::ClientContext ctx;
    beebium::SetSlotProtectionRequest req;
    beebium::SetSlotProtectionResponse resp;
    req.set_group_id("board");
    req.set_kind(beebium::SIDEWAYS_PROTECTION_KIND_HIDE);
    req.set_engaged(true);
    REQUIRE(fixture.sideways().SetSlotProtection(&ctx, req, &resp).ok());
    CHECK_FALSE(resp.success());
}

TEST_CASE("SidewaysService reports Integra-B per-chip protection groups",
          "[grpc][sideways][integra_b][protection]") {
    IntegraBSidewaysFixture fixture(/*pair_8_9_ram=*/true);

    grpc::ClientContext ctx;
    beebium::GetSlotStatusRequest req;
    beebium::GetSlotStatusResponse resp;
    REQUIRE(fixture.sideways().GetSlotStatus(&ctx, req, &resp).ok());

    REQUIRE(resp.protection_groups_size() == 3);
    const char* ids[] = {"slots-4-5", "slots-6-7", "slots-8-9"};
    for (int i = 0; i < 3; ++i) {
        const auto& g = resp.protection_groups(i);
        CHECK(g.id() == ids[i]);
        CHECK(g.supports_write_protect());
        CHECK_FALSE(g.supports_hide());
        CHECK_FALSE(g.write_protected());
        CHECK(g.slots_size() == 2);
    }
    CHECK(resp.protection_groups(0).label() == "Write-protect slots 4/5 (WP 4/5)");
    // The Integra-B has no write-select latch.
    CHECK_FALSE(resp.write_select_latch().present());
}

TEST_CASE("SidewaysService SetSlotProtection write-protects an Integra-B RAM chip",
          "[grpc][sideways][integra_b][protection]") {
    IntegraBSidewaysFixture fixture;
    auto& hw = fixture.machine().state().memory;

    {
        grpc::ClientContext ctx;
        beebium::SetSlotProtectionRequest req;
        beebium::SetSlotProtectionResponse resp;
        req.set_group_id("slots-6-7");
        req.set_kind(beebium::SIDEWAYS_PROTECTION_KIND_WRITE_PROTECT);
        req.set_engaged(true);
        REQUIRE(fixture.sideways().SetSlotProtection(&ctx, req, &resp).ok());
        CHECK(resp.success());
    }
    hw.write(0xFE30, 7);
    hw.write(0x8000, 0x5A);
    CHECK(hw.read(0x8000) != 0x5A);

    // No hide switch on this board.
    grpc::ClientContext ctx;
    beebium::SetSlotProtectionRequest req;
    beebium::SetSlotProtectionResponse resp;
    req.set_group_id("slots-6-7");
    req.set_kind(beebium::SIDEWAYS_PROTECTION_KIND_HIDE);
    req.set_engaged(true);
    auto status = fixture.sideways().SetSlotProtection(&ctx, req, &resp);
    CHECK_FALSE((status.ok() && resp.success()));
}
