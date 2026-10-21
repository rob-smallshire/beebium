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

// Test gRPC IndicatorService
//
// These tests verify the IndicatorService implementation by acting as a gRPC client.
// They create a local server, connect to it, and verify indicator operations work correctly.

#include <catch2/catch_test_macros.hpp>
#include "test_extension_ids.hpp"
#include <catch2/matchers/catch_matchers.hpp>

#include "beebium/Machines.hpp"
#include "beebium/service/Server.hpp"
#include "beebium/disc/DiscLoader.hpp"
#include "beebium/FrameAllocator.hpp"
#include "beebium/FrameBuffer.hpp"
#include "beebium/FrameRenderer.hpp"
#include "beebium/extension/ExtensionContext.hpp"
#include "beebium/extension/ExtensionRegistry.hpp"

#include <AcornScsiHostAdapter.hpp>
#include <ScsiHardDiscExtension.hpp>

#include "indicator.grpc.pb.h"
#include "disc.grpc.pb.h"
#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

#include "test_keyboard_helpers.hpp"

namespace {

// =============================================================================
// Workaround for protobuf Map hash bug on x86_64 macOS
//
// Since protobuf v22, google::protobuf::Map uses absl::hash with a non-deterministic
// seed that can cause find()/count()/at() to fail even when keys exist and are
// correctly accessible via iteration. This manifests on x86_64 macOS but not arm64.
//
// The bug is documented in:
//   - https://github.com/protocolbuffers/protobuf/issues/15069
//   - https://github.com/protocolbuffers/protobuf/issues/18097
//
// Symptoms: map.size() returns correct count, iteration works and shows correct
// keys/values, but find(key) returns end() and count(key) returns 0.
//
// Workaround: Use iteration-based lookup instead of hash-based lookup.
// =============================================================================

// Find a value in a protobuf Map using iteration (workaround for hash bug)
template<typename MapType>
auto proto_map_find(const MapType& map, const typename MapType::key_type& key)
    -> decltype(map.begin()) {
    for (auto it = map.begin(); it != map.end(); ++it) {
        if (it->first == key) {
            return it;
        }
    }
    return map.end();
}

// Check if a key exists in a protobuf Map using iteration (workaround for hash bug)
template<typename MapType>
bool proto_map_contains(const MapType& map, const typename MapType::key_type& key) {
    return proto_map_find(map, key) != map.end();
}

// Get a value from a protobuf Map using iteration, with default (workaround for hash bug)
template<typename MapType>
auto proto_map_get(const MapType& map, const typename MapType::key_type& key,
                   const typename MapType::mapped_type& default_value = {})
    -> typename MapType::mapped_type {
    auto it = proto_map_find(map, key);
    return (it != map.end()) ? it->second : default_value;
}

// Helper to wait for an indicator value with timeout (more reliable than fixed sleep)
template<typename StubType>
bool wait_for_indicator(StubType& stub, const std::string& indicator_name,
                        std::function<bool(uint64_t)> predicate,
                        std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < timeout) {
        grpc::ClientContext context;
        beebium::GetIndicatorsRequest request;
        beebium::GetIndicatorsResponse response;
        auto status = stub.GetIndicators(&context, request, &response);
        if (status.ok()) {
            // Use iteration-based lookup (workaround for protobuf Map hash bug)
            auto it = proto_map_find(response.values(), indicator_name);
            if (it != response.values().end() && predicate(it->second)) {
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

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

// Test fixture for Model B
class IndicatorTestFixtureModelB {
public:
    IndicatorTestFixtureModelB() {
        // Load ROMs
#ifdef BEEBIUM_ROM_DIR
        auto mos = load_rom(std::string(BEEBIUM_ROM_DIR) + "/acorn-mos_1_20.rom");
        auto basic = load_rom(std::string(BEEBIUM_ROM_DIR) + "/bbc-basic_2.rom");
        std::copy(mos.begin(), mos.end(), machine_.state().memory.mos_rom.data());
        machine_.state().memory.load_basic(basic.data(), basic.size());
#endif
        machine_.reset();

        // No extensions in this fixture, so we can close the registration
        // window immediately by starting the indicators consumer thread.
        machine_.state().memory.indicators.start();

        // Start server on a dynamically allocated port
        server_ = std::make_unique<beebium::service::Server<beebium::ModelB>>(
            machine_, "127.0.0.1", 0);
        server_->start({}, {});

        // Create client channel using the actual bound port
        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        stub_ = beebium::IndicatorService::NewStub(channel_);
    }

    ~IndicatorTestFixtureModelB() {
        server_->stop();
    }

    beebium::ModelB& machine() { return machine_; }
    beebium::IndicatorService::Stub& stub() { return *stub_; }

private:
    beebium::ModelB machine_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::IndicatorService::Stub> stub_;
};

// Test fixture for Model B+ (has disc drive activity LEDs)
class IndicatorTestFixtureBPlus {
public:
    IndicatorTestFixtureBPlus() {
        // Load ROMs
#ifdef BEEBIUM_ROM_DIR
        auto mos = load_rom(std::string(BEEBIUM_ROM_DIR) + "/acorn-mos_2_0.rom");
        auto basic = load_rom(std::string(BEEBIUM_ROM_DIR) + "/bbc-basic_2.rom");
        std::copy(mos.begin(), mos.end(), machine_.state().memory.mos_rom.data());
        machine_.state().memory.load_basic(basic.data(), basic.size());
#endif
        machine_.reset();

        // No extensions in this fixture, so we can close the registration
        // window immediately by starting the indicators consumer thread.
        machine_.state().memory.indicators.start();

        // Start server on a dynamically allocated port
        server_ = std::make_unique<beebium::service::Server<beebium::ModelBPlus>>(
            machine_, "127.0.0.1", 0);
        server_->start({}, {});

        // Create client channel using the actual bound port
        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        indicator_stub_ = beebium::IndicatorService::NewStub(channel_);
        disc_stub_ = beebium::DiscService::NewStub(channel_);
    }

    ~IndicatorTestFixtureBPlus() {
        server_->stop();
    }

    beebium::ModelBPlus& machine() { return machine_; }
    beebium::IndicatorService::Stub& stub() { return *indicator_stub_; }
    beebium::DiscService::Stub& disc_stub() { return *disc_stub_; }

private:
    beebium::ModelBPlus machine_;
    std::unique_ptr<beebium::service::Server<beebium::ModelBPlus>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::IndicatorService::Stub> indicator_stub_;
    std::unique_ptr<beebium::DiscService::Stub> disc_stub_;
};

} // anonymous namespace

// =============================================================================
// Diagnostic tests - check indicators directly without gRPC
// =============================================================================

// Regression test for protobuf Map hash bug on x86_64 macOS.
// See docs/known-issues.md for details.
// This test verifies the iteration-based workaround functions correctly.
TEST_CASE("Protobuf Map workaround: iteration-based lookup works", "[grpc][indicator]") {
    IndicatorTestFixtureModelB fixture;

    // Get metadata directly from the machine (std::unordered_map - works fine)
    auto& indicators = fixture.machine().state().memory.indicators;
    auto direct_meta = indicators.metadata("caps-lock-led");
    REQUIRE(direct_meta.size() == 4);

    // Get metadata via gRPC (google::protobuf::Map - has hash bug on x86_64)
    grpc::ClientContext context;
    beebium::ListIndicatorsRequest request;
    beebium::ListIndicatorsResponse response;
    auto status = fixture.stub().ListIndicators(&context, request, &response);
    REQUIRE(status.ok());

    // Find caps-lock-led in response
    for (const auto& indicator : response.indicators()) {
        if (indicator.name() == "caps-lock-led") {
            const auto& grpc_meta = indicator.metadata();

            // Verify iteration-based workaround finds all keys
            for (const auto& [key, value] : direct_meta) {
                auto grpc_value = proto_map_get(grpc_meta, key, std::string{});
                CHECK(grpc_value == value);
            }
            return;
        }
    }
    FAIL("caps-lock-led not found in gRPC response");
}

// =============================================================================
// ListIndicators tests
// =============================================================================

TEST_CASE("IndicatorService ListIndicators returns registered indicators for Model B", "[grpc][indicator]") {
    IndicatorTestFixtureModelB fixture;

    grpc::ClientContext context;
    beebium::ListIndicatorsRequest request;
    beebium::ListIndicatorsResponse response;

    auto status = fixture.stub().ListIndicators(&context, request, &response);

    REQUIRE(status.ok());
    // Model B has caps-lock-led and shift-lock-led
    REQUIRE(response.indicators_size() >= 2);

    // Find caps-lock-led
    bool found_caps = false;
    bool found_shift = false;
    for (const auto& indicator : response.indicators()) {
        if (indicator.name() == "caps-lock-led") {
            found_caps = true;
            // Use iteration-based lookup (workaround for protobuf Map hash bug)
            CHECK(proto_map_contains(indicator.metadata(), std::string("label")));
            CHECK(proto_map_get(indicator.metadata(), std::string("label")) == "CAPS LOCK");
        }
        if (indicator.name() == "shift-lock-led") {
            found_shift = true;
            CHECK(proto_map_contains(indicator.metadata(), std::string("label")));
            CHECK(proto_map_get(indicator.metadata(), std::string("label")) == "SHIFT LOCK");
        }
    }
    CHECK(found_caps);
    CHECK(found_shift);
}

TEST_CASE("IndicatorService ListIndicators returns disc indicators for Model B+", "[grpc][indicator]") {
    IndicatorTestFixtureBPlus fixture;

    grpc::ClientContext context;
    beebium::ListIndicatorsRequest request;
    beebium::ListIndicatorsResponse response;

    auto status = fixture.stub().ListIndicators(&context, request, &response);

    REQUIRE(status.ok());
    // Model B+ has caps, shift, and two disc activity LEDs
    REQUIRE(response.indicators_size() >= 4);

    bool found_floppy_0 = false;
    bool found_floppy_1 = false;
    for (const auto& indicator : response.indicators()) {
        if (indicator.name() == "floppy-0-activity-led") {
            found_floppy_0 = true;
            // Use iteration-based lookup (workaround for protobuf Map hash bug)
            CHECK(proto_map_contains(indicator.metadata(), std::string("label")));
            CHECK(proto_map_get(indicator.metadata(), std::string("label")) == "FDD 0");
            CHECK(proto_map_contains(indicator.metadata(), std::string("color")));
            CHECK(proto_map_get(indicator.metadata(), std::string("color")) == "568nm");
        }
        if (indicator.name() == "floppy-1-activity-led") {
            found_floppy_1 = true;
            CHECK(proto_map_get(indicator.metadata(), std::string("label")) == "FDD 1");
        }
    }
    CHECK(found_floppy_0);
    CHECK(found_floppy_1);
}

TEST_CASE("IndicatorService ListIndicators includes metadata", "[grpc][indicator]") {
    IndicatorTestFixtureModelB fixture;

    grpc::ClientContext context;
    beebium::ListIndicatorsRequest request;
    beebium::ListIndicatorsResponse response;

    auto status = fixture.stub().ListIndicators(&context, request, &response);

    REQUIRE(status.ok());

    // Find caps-lock-led and check all metadata fields
    for (const auto& indicator : response.indicators()) {
        if (indicator.name() == "caps-lock-led") {
            // Use iteration-based lookup (workaround for protobuf Map hash bug)
            CHECK(proto_map_get(indicator.metadata(), std::string("color")) == "625nm");
            CHECK(proto_map_get(indicator.metadata(), std::string("shape")) == "domed");
            return;
        }
    }
    FAIL("caps-lock-led not found");
}

// =============================================================================
// GetIndicators tests
// =============================================================================

TEST_CASE("IndicatorService GetIndicators returns current values", "[grpc][indicator]") {
    IndicatorTestFixtureModelB fixture;

    grpc::ClientContext context;
    beebium::GetIndicatorsRequest request;
    beebium::GetIndicatorsResponse response;

    auto status = fixture.stub().GetIndicators(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.changed());
    // Sequence starts at 0, no need to check > 0
    // Use iteration-based lookup (workaround for protobuf Map hash bug)
    CHECK(proto_map_contains(response.values(), std::string("caps-lock-led")));
    CHECK(proto_map_contains(response.values(), std::string("shift-lock-led")));
}

TEST_CASE("IndicatorService GetIndicators conditional fetch returns unchanged when sequence matches", "[grpc][indicator]") {
    IndicatorTestFixtureModelB fixture;

    // First, trigger a change to make sequence > 0
    fixture.machine().state().memory.indicators.set("caps-lock-led", 255);

    // Wait for indicator to be set (polling with timeout for CI reliability)
    bool indicator_updated = wait_for_indicator(
        fixture.stub(), "caps-lock-led",
        [](uint64_t value) { return value > 0; });
    REQUIRE(indicator_updated);

    // Get the current sequence after the change
    uint64_t current_seq;
    {
        grpc::ClientContext context;
        beebium::GetIndicatorsRequest request;
        beebium::GetIndicatorsResponse response;
        fixture.stub().GetIndicators(&context, request, &response);
        current_seq = response.sequence();
        REQUIRE(current_seq > 0);  // Must have a valid sequence
    }

    // Second fetch with if_changed_since - should return unchanged
    {
        grpc::ClientContext context;
        beebium::GetIndicatorsRequest request;
        request.set_if_changed_since(current_seq);
        beebium::GetIndicatorsResponse response;

        auto status = fixture.stub().GetIndicators(&context, request, &response);

        REQUIRE(status.ok());
        CHECK_FALSE(response.changed());
        CHECK(response.sequence() == current_seq);
        CHECK(response.values().empty());
    }
}

TEST_CASE("IndicatorService GetIndicators conditional fetch returns data when sequence changed", "[grpc][indicator]") {
    IndicatorTestFixtureModelB fixture;

    // First fetch
    uint64_t old_seq;
    {
        grpc::ClientContext context;
        beebium::GetIndicatorsRequest request;
        beebium::GetIndicatorsResponse response;
        fixture.stub().GetIndicators(&context, request, &response);
        old_seq = response.sequence();
    }

    // Trigger an indicator change by manipulating the addressable latch
    // The caps lock LED is controlled by bit 6 of the latch
    fixture.machine().state().memory.addressable_latch.write(6, true);  // Caps Lock LED on
    fixture.machine().state().memory.indicators.set("caps-lock-led", 255);

    // Wait for indicator value to change (polling with timeout for CI reliability)
    bool indicator_updated = wait_for_indicator(
        fixture.stub(), "caps-lock-led",
        [](uint64_t value) { return value > 0; });
    REQUIRE(indicator_updated);

    // Second fetch with old sequence
    {
        grpc::ClientContext context;
        beebium::GetIndicatorsRequest request;
        request.set_if_changed_since(old_seq);
        beebium::GetIndicatorsResponse response;

        auto status = fixture.stub().GetIndicators(&context, request, &response);

        REQUIRE(status.ok());
        CHECK(response.changed());
        CHECK(response.sequence() > old_seq);
        CHECK_FALSE(response.values().empty());
    }
}

TEST_CASE("IndicatorService GetIndicators reflects LED state changes", "[grpc][indicator]") {
    IndicatorTestFixtureModelB fixture;

    // Turn on caps lock LED via indicator system
    fixture.machine().state().memory.indicators.set("caps-lock-led", 255);

    // Wait for indicator to be set (polling with timeout for CI reliability)
    bool indicator_updated = wait_for_indicator(
        fixture.stub(), "caps-lock-led",
        [](uint64_t value) { return value > 0; });
    REQUIRE(indicator_updated);
}

// =============================================================================
// Subscribe tests
// =============================================================================

TEST_CASE("IndicatorService Subscribe streams updates", "[grpc][indicator]") {
    IndicatorTestFixtureModelB fixture;

    // We need to run the subscriber in a separate thread since Read() blocks
    std::atomic<int> update_count{0};
    std::atomic<bool> stop_reading{false};

    grpc::ClientContext context;
    beebium::SubscribeIndicatorsRequest request;
    request.set_min_interval_ms(10);  // Fast updates for testing

    auto reader = fixture.stub().Subscribe(&context, request);

    // Start a thread to read updates
    std::thread reader_thread([&]() {
        beebium::IndicatorUpdate update;
        while (!stop_reading && reader->Read(&update)) {
            update_count++;
        }
    });

    // Give reader time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // Trigger some indicator changes
    for (int i = 0; i < 5; ++i) {
        fixture.machine().state().memory.indicators.set("caps-lock-led", i % 2 == 0 ? 255 : 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }

    // Give time for updates to propagate
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Stop reading and cancel
    stop_reading = true;
    context.TryCancel();

    reader_thread.join();

    // Should have received at least one update
    CHECK(update_count.load() >= 1);
}

TEST_CASE("IndicatorService Subscribe filters by name", "[grpc][indicator]") {
    IndicatorTestFixtureBPlus fixture;

    grpc::ClientContext context;
    beebium::SubscribeIndicatorsRequest request;
    request.add_names("caps-lock-led");  // Only subscribe to caps lock
    request.set_min_interval_ms(10);

    auto reader = fixture.stub().Subscribe(&context, request);

    // Change both caps lock and floppy LED
    fixture.machine().state().memory.indicators.set("caps-lock-led", 255);
    fixture.machine().state().memory.indicators.set("floppy-0-activity-led", 255);

    // Wait for the caps-lock indicator to be visible via GetIndicators
    // (This ensures the consumer thread has processed the update)
    bool indicator_updated = wait_for_indicator(
        fixture.stub(), "caps-lock-led",
        [](uint64_t value) { return value > 0; });
    REQUIRE(indicator_updated);

    context.TryCancel();

    // Read updates
    beebium::IndicatorUpdate update;
    while (reader->Read(&update)) {
        // Should only contain caps-lock-led, not floppy
        // Use iteration-based lookup (workaround for protobuf Map hash bug)
        CHECK_FALSE(proto_map_contains(update.values(), std::string("floppy-0-activity-led")));
    }
}

// =============================================================================
// Model B+ specific tests
// =============================================================================

TEST_CASE("IndicatorService Model B+ disc motor indicator updates", "[grpc][indicator]") {
    IndicatorTestFixtureBPlus fixture;

    // Select drive 0 and turn on its motor: the LED needs both
    fixture.machine().state().memory.disc_drive_0.set_selected(true);
    fixture.machine().state().memory.disc_drive_0.set_motor(true);

    // Wait for indicator to become non-zero (polling with timeout for CI reliability)
    bool indicator_updated = wait_for_indicator(
        fixture.stub(), "floppy-0-activity-led",
        [](uint64_t value) { return value > 0; });
    REQUIRE(indicator_updated);

    // Verify drive 1 is still off
    grpc::ClientContext context;
    beebium::GetIndicatorsRequest request;
    beebium::GetIndicatorsResponse response;
    auto status = fixture.stub().GetIndicators(&context, request, &response);
    REQUIRE(status.ok());
    // Use iteration-based lookup (workaround for protobuf Map hash bug)
    CHECK(proto_map_get(response.values(), std::string("floppy-1-activity-led"), uint64_t{1}) == 0);
}

TEST_CASE("IndicatorService Model B+ both drives can be active", "[grpc][indicator]") {
    IndicatorTestFixtureBPlus fixture;

    // Select both drives and turn on both motors: each LED needs both
    fixture.machine().state().memory.disc_drive_0.set_selected(true);
    fixture.machine().state().memory.disc_drive_1.set_selected(true);
    fixture.machine().state().memory.disc_drive_0.set_motor(true);
    fixture.machine().state().memory.disc_drive_1.set_motor(true);

    // Wait for both indicators to become non-zero (polling with timeout)
    bool drive0_updated = wait_for_indicator(
        fixture.stub(), "floppy-0-activity-led",
        [](uint64_t value) { return value > 0; });
    REQUIRE(drive0_updated);

    bool drive1_updated = wait_for_indicator(
        fixture.stub(), "floppy-1-activity-led",
        [](uint64_t value) { return value > 0; });
    REQUIRE(drive1_updated);

    grpc::ClientContext context;
    beebium::GetIndicatorsRequest request;
    beebium::GetIndicatorsResponse response;

    auto status = fixture.stub().GetIndicators(&context, request, &response);

    REQUIRE(status.ok());
    // Use iteration-based lookup (workaround for protobuf Map hash bug)
    CHECK(proto_map_get(response.values(), std::string("floppy-0-activity-led"), uint64_t{0}) > 0);
    CHECK(proto_map_get(response.values(), std::string("floppy-1-activity-led"), uint64_t{0}) > 0);
}

// =============================================================================
// Edge cases
// =============================================================================

TEST_CASE("IndicatorService GetIndicators with zero sequence returns all", "[grpc][indicator]") {
    IndicatorTestFixtureModelB fixture;

    grpc::ClientContext context;
    beebium::GetIndicatorsRequest request;
    request.set_if_changed_since(0);  // Explicitly set to 0
    beebium::GetIndicatorsResponse response;

    auto status = fixture.stub().GetIndicators(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.changed());
    CHECK_FALSE(response.values().empty());
}

TEST_CASE("IndicatorService returns correct sequence after rapid updates", "[grpc][indicator]") {
    IndicatorTestFixtureModelB fixture;

    // Rapid updates with enough time between for consumer to notice
    for (int i = 0; i < 5; ++i) {
        fixture.machine().state().memory.indicators.set("caps-lock-led", i % 2 == 0 ? 255 : 0);
        // Small delay between updates so consumer can see distinct changes
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    // Final value is 255 (i=4, even)

    // Wait for final indicator value to be visible (polling with timeout)
    bool indicator_updated = wait_for_indicator(
        fixture.stub(), "caps-lock-led",
        [](uint64_t value) { return value > 0; });
    REQUIRE(indicator_updated);

    grpc::ClientContext context;
    beebium::GetIndicatorsRequest request;
    beebium::GetIndicatorsResponse response;

    auto status = fixture.stub().GetIndicators(&context, request, &response);

    REQUIRE(status.ok());
    // Sequence should have advanced
    CHECK(response.sequence() >= 1);
}

// =============================================================================
// Integration test: disc control register -> indicator
// =============================================================================

TEST_CASE("IndicatorService drive LED activates when WD1770 executes command", "[grpc][indicator][integration]") {
    // This test verifies the full signal path:
    // WD1770 command execution -> spin_up() -> DiscDrive::set_motor()
    // -> Indicators::set() -> consumer thread -> gRPC GetIndicators
    //
    // Note: The WD1770 handles motor control internally, not via the disc
    // control register bit 4 (which was for 8271-style explicit control).
    IndicatorTestFixtureBPlus fixture;

    // Disable spin-up delay for faster testing
    fixture.machine().state().memory.disc_controller.set_spin_up_delay_enabled(false);

    // Get initial indicator state - should be off
    {
        grpc::ClientContext ctx;
        beebium::GetIndicatorsRequest req;
        beebium::GetIndicatorsResponse resp;
        auto status = fixture.stub().GetIndicators(&ctx, req, &resp);
        REQUIRE(status.ok());
        // Use iteration-based lookup (workaround for protobuf Map hash bug)
        CHECK(proto_map_get(resp.values(), std::string("floppy-0-activity-led"), uint64_t{1}) == 0);
    }

    // Select drive 0 and release reset via disc control register
    // &FE80 = disc control register: bit 0 = drive 0 select, bit 5 = reset (active low)
    fixture.machine().write(0xFE80, 0x21);  // Drive 0 selected, reset inactive

    // Write a Restore command (0x00) to the WD1770 command register (&FE84)
    // This Type I command will spin up the motor internally
    fixture.machine().write(0xFE84, 0x00);

    // Tick the machine a few times to let the WD1770 process the command
    for (int i = 0; i < 1000; ++i) {
        fixture.machine().step();
    }

    // Wait for indicator to turn on (polling with timeout)
    bool led_on = wait_for_indicator(
        fixture.stub(), "floppy-0-activity-led",
        [](uint64_t value) { return value > 0; });
    REQUIRE(led_on);

    // Force Interrupt (0xD0) terminates the command but motor stays on
    // Motor will spin down after ~2 second idle timeout
    fixture.machine().write(0xFE84, 0xD0);

    // Tick the machine past the motor idle timeout (2M 1MHz ticks)
    // Machine::step() is at 2MHz, poll_nmi() ticks disc at 1MHz every 2 steps
    for (int i = 0; i < 4'500'000; ++i) {
        fixture.machine().step();
    }

    // Wait for indicator to turn off (polling with timeout)
    bool led_off = wait_for_indicator(
        fixture.stub(), "floppy-0-activity-led",
        [](uint64_t value) { return value == 0; });
    REQUIRE(led_off);
}

// =============================================================================
// Spin-up delay configuration tests
// =============================================================================

TEST_CASE("WD1770 spin-up delay is enabled by default", "[grpc][disc]") {
    IndicatorTestFixtureBPlus fixture;

    // Default should be enabled
    CHECK(fixture.machine().state().memory.disc_controller.spin_up_delay_enabled() == true);
}

TEST_CASE("WD1770 spin-up delay can be toggled via accessor", "[grpc][disc]") {
    IndicatorTestFixtureBPlus fixture;

    // Disable
    fixture.machine().state().memory.disc_controller.set_spin_up_delay_enabled(false);
    CHECK(fixture.machine().state().memory.disc_controller.spin_up_delay_enabled() == false);

    // Re-enable
    fixture.machine().state().memory.disc_controller.set_spin_up_delay_enabled(true);
    CHECK(fixture.machine().state().memory.disc_controller.spin_up_delay_enabled() == true);
}

TEST_CASE("DiscService spin-up delay can be queried and set via gRPC", "[grpc][disc]") {
    IndicatorTestFixtureBPlus fixture;

    // Query default value (should be enabled)
    {
        grpc::ClientContext ctx;
        beebium::GetSpinUpDelayRequest req;
        beebium::GetSpinUpDelayResponse resp;
        auto status = fixture.disc_stub().GetSpinUpDelay(&ctx, req, &resp);
        REQUIRE(status.ok());
        CHECK(resp.enabled() == true);
    }

    // Disable via gRPC
    {
        grpc::ClientContext ctx;
        beebium::SetSpinUpDelayRequest req;
        beebium::SetSpinUpDelayResponse resp;
        req.set_enabled(false);
        auto status = fixture.disc_stub().SetSpinUpDelay(&ctx, req, &resp);
        REQUIRE(status.ok());
        CHECK(resp.success() == true);
    }

    // Verify disabled
    {
        grpc::ClientContext ctx;
        beebium::GetSpinUpDelayRequest req;
        beebium::GetSpinUpDelayResponse resp;
        auto status = fixture.disc_stub().GetSpinUpDelay(&ctx, req, &resp);
        REQUIRE(status.ok());
        CHECK(resp.enabled() == false);
    }

    // Also verify via direct accessor
    CHECK(fixture.machine().state().memory.disc_controller.spin_up_delay_enabled() == false);
}

// =============================================================================
// *CAT motor timing test - measures indicator duration via gRPC
// =============================================================================

namespace {

// Helper to find a string anywhere in MODE 7 screen memory
bool find_string_on_screen(beebium::ModelBPlus& machine, const std::string& needle) {
    // MODE 7 screen memory is at 0x7C00-0x7FFF (1K)
    for (uint16_t addr = 0x7C00; addr <= 0x7FFF - needle.size(); ++addr) {
        bool found = true;
        for (size_t i = 0; i < needle.size(); ++i) {
            if (machine.read(addr + i) != static_cast<uint8_t>(needle[i])) {
                found = false;
                break;
            }
        }
        if (found) {
            return true;
        }
    }
    return false;
}

} // anonymous namespace

TEST_CASE("Measure drive LED indicator duration during *CAT via gRPC", "[grpc][indicator][timing][!mayfail]") {
    // This test boots to BASIC, runs *CAT, and monitors the drive activity LED
    // to measure how long the motor is active.

#ifndef BEEBIUM_ROM_DIR
    SKIP("BEEBIUM_ROM_DIR not defined");
#else
#ifndef BEEBIUM_DISCS_DIR
    SKIP("BEEBIUM_DISCS_DIR not defined");
#else
    using namespace beebium;
    using namespace beebium::test;

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    const auto disc_dirpath = std::filesystem::path(BEEBIUM_DISCS_DIR);

    auto mos_filepath = rom_dirpath / "acorn-mos_2_0.rom";
    auto basic_filepath = rom_dirpath / "bbc-basic_2.rom";
    auto dfs_filepath = rom_dirpath / "acorn-dfs_2_26.rom";
    auto disc_filepath = disc_dirpath / "01-basic-validation.ssd";

    if (!std::filesystem::exists(mos_filepath) ||
        !std::filesystem::exists(basic_filepath) ||
        !std::filesystem::exists(dfs_filepath)) {
        SKIP("Required ROMs not available");
    }
    if (!std::filesystem::exists(disc_filepath)) {
        SKIP("Test disc image not available: " + disc_filepath.string());
    }

    // Create machine and load ROMs
    ModelBPlus machine;
    {
        auto mos = load_rom(mos_filepath.string());
        auto basic = load_rom(basic_filepath.string());
        auto dfs = load_rom(dfs_filepath.string());
        std::copy(mos.begin(), mos.end(), machine.state().memory.mos_rom.data());
        machine.state().memory.load_basic(basic.data(), basic.size());
        machine.state().memory.load_dfs(dfs.data(), dfs.size());
    }

    // Load and insert disc image
    auto disc_result = load_disc_from_url_or_filepath(disc_filepath.string());
    REQUIRE(disc_result.success());
    machine.state().memory.disc_drive_0.insert(std::move(disc_result.disc));

    // Disable spin-up delay for faster testing
    machine.state().memory.disc_controller.set_spin_up_delay_enabled(false);

    // Enable video output and reset
    machine.state().memory.enable_video_output();
    machine.reset();

    // Set up frame rendering (minimal)
    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Boot to BASIC prompt (~4M cycles, ~2 sec emulated)
    for (uint64_t i = 0; i < 4'000'000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }
    REQUIRE(find_string_on_screen(machine, "Acorn 1770 DFS"));

    // Type "*CAT" (without RETURN yet)
    type_string_with_shift(machine, renderer, "*CAT", 100000);

    // Start motor monitoring BEFORE pressing RETURN
    bool motor_was_on = false;
    int transition_count = 0;
    uint64_t motor_on_cycle = 0;
    uint64_t total_motor_cycles = 0;
    uint64_t global_cycle = 0;
    uint8_t last_disc_ctrl = 0xFF;  // Initial impossible value to ensure first change is logged

    // Helper to step and check motor
    auto step_and_check = [&]() {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        if (global_cycle % 100 == 0) {  // Check every 100 cycles for finer resolution
            bool motor_on = machine.state().memory.disc_drive_0.motor_on();
            if (motor_on && !motor_was_on) {
                transition_count++;
                motor_on_cycle = global_cycle;
                WARN("[cycle " << global_cycle << "] Motor ON");
            } else if (!motor_on && motor_was_on) {
                uint64_t duration = global_cycle - motor_on_cycle;
                total_motor_cycles += duration;
                WARN("[cycle " << global_cycle << "] Motor OFF after " << duration << " cycles ("
                     << (duration / 2000) << "ms)");
            }
            motor_was_on = motor_on;

            // Log ANY change to disc control register
            uint8_t disc_ctrl = machine.state().memory.disc_control_reg.control;
            if (disc_ctrl != last_disc_ctrl) {
                WARN("[cycle " << global_cycle << "] disc_control changed: 0x" << std::hex
                     << (int)last_disc_ctrl << " -> 0x" << (int)disc_ctrl << std::dec
                     << " (motor=" << ((disc_ctrl & 0x10) ? "ON" : "OFF")
                     << " drive=" << (disc_ctrl & 0x03)
                     << " side=" << ((disc_ctrl & 0x04) >> 2)
                     << " nmi=" << ((disc_ctrl & 0x40) ? "EN" : "DIS") << ")");
                last_disc_ctrl = disc_ctrl;
            }
        }
        global_cycle++;
    };

    // Now press RETURN and monitor during the disc access
    // Enable WD1770 debug output BEFORE pressing RETURN to capture all commands
    // Debug tracing
    WARN("Pressing RETURN...");
    machine.state().memory.system_via_peripheral.key_down(4, 9);  // RETURN key
    for (int i = 0; i < 50000; ++i) step_and_check();  // Hold for 25ms
    machine.state().memory.system_via_peripheral.key_up(4, 9);
    for (int i = 0; i < 50000; ++i) step_and_check();  // Release for 25ms

    // Continue monitoring for disc access (~4M cycles = 2 sec emulated)
    WARN("Monitoring disc access...");
    constexpr uint64_t TOTAL_CYCLES = 4'000'000;
    for (uint64_t cycle = 0; cycle < TOTAL_CYCLES; ++cycle) {
        step_and_check();
    }

    // Debug tracing off

    // Check screen for *CAT output
    bool has_drive = find_string_on_screen(machine, "Drive");  // DFS shows "Drive 0"
    bool has_option = find_string_on_screen(machine, "Option");  // DFS shows "Option X"
    bool has_dir = find_string_on_screen(machine, "Dir.");  // DFS shows "Dir. $"
    WARN("Screen check: Drive=" << has_drive << " Option=" << has_option << " Dir=" << has_dir);

    // Summary
    WARN("=== Motor Activity Summary ===");
    WARN("Transitions: " << transition_count);
    WARN("Total motor ON: " << total_motor_cycles << " cycles (" << (total_motor_cycles / 2000) << "ms)");

    if (transition_count == 0) {
        WARN("WARNING: Motor was never turned ON during *CAT!");
    }

    CHECK(transition_count >= 0);

#endif
#endif
}

// =============================================================================
// HDD activity LED end-to-end: SCSI bus activity drives hdd-N-activity-led
// through the full IndicatorService gRPC pipeline.
// =============================================================================

#if defined(BEEBIUM_ROM_DIR) && defined(BEEBIUM_TEST_ASSETS_DIR)

namespace {

bool scsi_assets_available() {
    std::filesystem::path assets_dir = BEEBIUM_TEST_ASSETS_DIR;
    return !assets_dir.empty()
        && std::filesystem::exists(assets_dir / "scsi" / "scsi0.dat")
        && std::filesystem::exists(assets_dir / "scsi" / "scsi0.dsc");
}

// Test fixture: Model B+ with the Acorn SCSI host adapter and a single
// SCSI hard disc at LUN 0. Goes through the full extension lifecycle so
// hdd-0-activity-led is registered with the indicators registry and exposed
// through IndicatorService.
class IndicatorTestFixtureBPlusWithScsi {
public:
    IndicatorTestFixtureBPlusWithScsi() {
        auto mos = load_rom(std::string(BEEBIUM_ROM_DIR) + "/acorn-mos_2_0.rom");
        auto basic = load_rom(std::string(BEEBIUM_ROM_DIR) + "/bbc-basic_2.rom");
        std::copy(mos.begin(), mos.end(), machine_.state().memory.mos_rom.data());
        machine_.state().memory.load_basic(basic.data(), basic.size());
        machine_.reset();

        // SCSI extensions
        registry_.register_extension_point("1mhz-bus");
        registry_.register_extension(with_default_id(beebium::AcornScsiHostAdapter::create()));
        auto hdd_ext = beebium::ScsiHardDiscExtension::create();
        std::filesystem::path image =
            std::filesystem::path(BEEBIUM_TEST_ASSETS_DIR) / "scsi" / "scsi0.dat";
        hdd_ext->set_config({
            {"scsi-id", "0"},
            {"image", image.string()},
            {"id", "test-hdd"},
        });
        registry_.register_extension(with_default_id(std::move(hdd_ext)));

        beebium::ExtensionContext ctx(
            &machine_.state().memory.one_mhz_bus(), nullptr, nullptr,
            &machine_.state().memory.indicators);
        registry_.resolve_and_init(ctx);

        // Close registration window after extensions have registered.
        machine_.state().memory.indicators.start();

        // Start gRPC server with the extension's services attached.
        std::vector<grpc::Service*> extension_services;

        server_ = std::make_unique<beebium::service::Server<beebium::ModelBPlus>>(
            machine_, "127.0.0.1", 0);
        server_->start({}, {}, false, {}, nullptr, extension_services);

        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        indicator_stub_ = beebium::IndicatorService::NewStub(channel_);
    }

    ~IndicatorTestFixtureBPlusWithScsi() {
        server_->stop();
        registry_.shutdown();
    }

    beebium::ModelBPlus& machine() { return machine_; }
    beebium::IndicatorService::Stub& stub() { return *indicator_stub_; }

private:
    beebium::ModelBPlus machine_;
    beebium::ExtensionRegistry registry_;
    std::unique_ptr<beebium::service::Server<beebium::ModelBPlus>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::IndicatorService::Stub> indicator_stub_;
};

}  // namespace

TEST_CASE("IndicatorService Model B+ with SCSI exposes hdd-0-activity-led",
          "[grpc][indicator][hdd]") {
    if (!scsi_assets_available()) SKIP("SCSI disc image not available");
    IndicatorTestFixtureBPlusWithScsi fixture;

    grpc::ClientContext context;
    beebium::ListIndicatorsRequest request;
    beebium::ListIndicatorsResponse response;

    auto status = fixture.stub().ListIndicators(&context, request, &response);
    REQUIRE(status.ok());

    bool found = false;
    std::string label, color, shape;
    for (const auto& info : response.indicators()) {
        if (info.name() == "hdd-0-activity-led") {
            found = true;
            label = proto_map_get(info.metadata(), std::string("label"), std::string{});
            color = proto_map_get(info.metadata(), std::string("color"), std::string{});
            shape = proto_map_get(info.metadata(), std::string("shape"), std::string{});
            break;
        }
    }
    REQUIRE(found);
    REQUIRE(label == "HDD 0");
    REQUIRE(color == "600nm");
    REQUIRE(shape == "rectangular");
}

TEST_CASE("IndicatorService Model B+ with SCSI signals activity on transaction",
          "[grpc][indicator][hdd]") {
    if (!scsi_assets_available()) SKIP("SCSI disc image not available");
    IndicatorTestFixtureBPlusWithScsi fixture;

    // Drive a TEST UNIT READY transaction directly via the memory map.
    // We don't run the 6502; we just write the SCSI register sequence.
    // OneMHzBusPort offsets are relative to 0xFC00, so the SCSI registers
    // live at 0x40-0x43 (which is 0xFC40-0xFC43 in absolute terms).
    auto& bus = fixture.machine().state().memory.one_mhz_bus();
    bus.write(0x40, 0x01);  // data: select target 0
    bus.write(0x42, 0x00);  // select reg: trigger Selection -> Command
    bus.write(0x40, 0x00);  // OP_TEST_UNIT_READY
    for (int i = 0; i < 5; ++i) {
        bus.write(0x40, 0x00);
    }
    bus.read(0x40);  // status byte
    bus.read(0x40);  // message byte -> BusFree

    // The bus fired (lun=0, true) on Selection and (lun=0, false) on BusFree.
    // The RetriggerableMonostable filter holds the LED on for the pulse width
    // configured in AcornScsiHostAdapter (kActivityPulseWidth, 250 ms).
    bool went_high = wait_for_indicator(
        fixture.stub(), "hdd-0-activity-led",
        [](uint64_t v) { return v == 255; });
    REQUIRE(went_high);

    // After the pulse expires, the LED must drop to 0.
    bool went_low = wait_for_indicator(
        fixture.stub(), "hdd-0-activity-led",
        [](uint64_t v) { return v == 0; });
    REQUIRE(went_low);
}

#endif  // BEEBIUM_ROM_DIR && BEEBIUM_TEST_ASSETS_DIR
