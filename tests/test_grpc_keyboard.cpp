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

// Test gRPC KeyboardService
//
// These tests verify the KeyboardService implementation by acting as a gRPC client.
// They create a local server, connect to it, and verify keyboard input works correctly.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include "beebium/Machines.hpp"
#include "beebium/service/Server.hpp"

#include "keyboard.grpc.pb.h"
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

// Test fixture that sets up a machine and server
class KeyboardTestFixture {
public:
    KeyboardTestFixture() {
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
        server_->start({}, {});

        // Create client channel using the actual bound port
        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        stub_ = beebium::KeyboardService::NewStub(channel_);
    }

    ~KeyboardTestFixture() {
        server_->stop();
    }

    beebium::ModelB& machine() { return machine_; }
    beebium::KeyboardService::Stub& stub() { return *stub_; }

private:
    beebium::ModelB machine_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::KeyboardService::Stub> stub_;
};

} // anonymous namespace

TEST_CASE("KeyboardService KeyDown sets key in matrix", "[grpc][keyboard]") {
    KeyboardTestFixture fixture;

    // Press key at row 0, column 0 (SHIFT key)
    grpc::ClientContext context;
    beebium::KeyRequest request;
    request.set_ik_number(0x00);  // row 0, column 0
    beebium::KeyResponse response;

    auto status = fixture.stub().KeyDown(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.accepted());

    // Verify key is pressed in the peripheral
    CHECK(fixture.machine().state().memory.system_via_peripheral.is_key_pressed(0, 0));
}

TEST_CASE("KeyboardService KeyUp clears key in matrix", "[grpc][keyboard]") {
    KeyboardTestFixture fixture;

    // First press a key (row 1, column 2)
    {
        grpc::ClientContext context;
        beebium::KeyRequest request;
        request.set_ik_number(0x12);  // row 1, column 2
        beebium::KeyResponse response;
        fixture.stub().KeyDown(&context, request, &response);
    }

    // Verify it's pressed
    CHECK(fixture.machine().state().memory.system_via_peripheral.is_key_pressed(1, 2));

    // Now release it
    {
        grpc::ClientContext context;
        beebium::KeyRequest request;
        request.set_ik_number(0x12);  // row 1, column 2
        beebium::KeyResponse response;
        fixture.stub().KeyUp(&context, request, &response);

        REQUIRE(response.accepted());
    }

    // Verify it's released
    CHECK_FALSE(fixture.machine().state().memory.system_via_peripheral.is_key_pressed(1, 2));
}

TEST_CASE("KeyboardService rejects invalid key positions", "[grpc][keyboard]") {
    KeyboardTestFixture fixture;

    // Try to press key at invalid position (row 10, column 10)
    grpc::ClientContext context;
    beebium::KeyRequest request;
    request.set_ik_number(0xAA);  // row 10, column 10 (invalid)
    beebium::KeyResponse response;

    auto status = fixture.stub().KeyDown(&context, request, &response);

    REQUIRE(status.ok());
    CHECK_FALSE(response.accepted());
}

TEST_CASE("KeyboardService GetState returns current keyboard state", "[grpc][keyboard]") {
    KeyboardTestFixture fixture;

    // Press a few keys
    fixture.machine().state().memory.system_via_peripheral.key_down(2, 3);
    fixture.machine().state().memory.system_via_peripheral.key_down(4, 5);

    // Get keyboard state via gRPC
    grpc::ClientContext context;
    beebium::GetStateRequest request;
    beebium::KeyboardState response;

    auto status = fixture.stub().GetState(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.pressed_rows_size() == 10);

    // Check that the pressed keys are reflected in the state
    // Row 2, column 3 should have bit 3 set in row 2
    CHECK((response.pressed_rows(2) & (1 << 3)) != 0);
    // Row 4, column 5 should have bit 5 set in row 4
    CHECK((response.pressed_rows(4) & (1 << 5)) != 0);
}

TEST_CASE("KeyboardService GetLockState reports the latch lock bits", "[grpc][keyboard][lock]") {
    KeyboardTestFixture fixture;
    auto& latch = fixture.machine().state().memory.system_via_peripheral.latch();

    // Lock-LED latch bits are active-low: a clear bit means the lock is
    // engaged. Set CAPS LOCK on (bit clear) and SHIFT LOCK off (bit set).
    latch.write(6, false);  // CAPS_LOCK_LED clear -> caps lock engaged
    latch.write(7, true);   // SHIFT_LOCK_LED set   -> shift lock released

    grpc::ClientContext context;
    beebium::GetLockStateRequest request;
    beebium::LockKeyState response;
    REQUIRE(fixture.stub().GetLockState(&context, request, &response).ok());
    CHECK(response.caps_lock_on());
    CHECK_FALSE(response.shift_lock_on());

    // Flip both and confirm the query tracks the latch exactly.
    latch.write(6, true);   // caps lock released
    latch.write(7, false);  // shift lock engaged

    grpc::ClientContext context2;
    beebium::LockKeyState response2;
    REQUIRE(fixture.stub().GetLockState(&context2, request, &response2).ok());
    CHECK_FALSE(response2.caps_lock_on());
    CHECK(response2.shift_lock_on());
}

TEST_CASE("KeyboardService multiple keys can be pressed simultaneously", "[grpc][keyboard]") {
    KeyboardTestFixture fixture;

    // Press SHIFT (0x00) and 'A' (0x41)
    {
        grpc::ClientContext context;
        beebium::KeyRequest request;
        request.set_ik_number(0x00);  // SHIFT: row 0, column 0
        beebium::KeyResponse response;
        fixture.stub().KeyDown(&context, request, &response);
    }
    {
        grpc::ClientContext context;
        beebium::KeyRequest request;
        request.set_ik_number(0x41);  // 'A': row 4, column 1
        beebium::KeyResponse response;
        fixture.stub().KeyDown(&context, request, &response);
    }

    // Both should be pressed
    auto& peripheral = fixture.machine().state().memory.system_via_peripheral;
    CHECK(peripheral.is_key_pressed(0, 0));
    CHECK(peripheral.is_key_pressed(4, 1));
}

// =============================================================================
// Keyboard Links API Tests
// =============================================================================

TEST_CASE("KeyboardService SetLinks and GetLinks roundtrip", "[grpc][keyboard][links]") {
    KeyboardTestFixture fixture;

    SECTION("default value is 0xFF (all links broken)") {
        grpc::ClientContext context;
        beebium::GetLinksRequest request;
        beebium::LinksState response;

        auto status = fixture.stub().GetLinks(&context, request, &response);

        REQUIRE(status.ok());
        CHECK(response.value() == 0xFF);
    }

    SECTION("set and read back raw byte") {
        // Set to 0xF8 (Mode 0)
        {
            grpc::ClientContext context;
            beebium::SetLinksRequest request;
            request.set_value(0xF8);
            beebium::SetLinksResponse response;

            auto status = fixture.stub().SetLinks(&context, request, &response);
            REQUIRE(status.ok());
            CHECK(response.success());
        }

        // Read back
        {
            grpc::ClientContext context;
            beebium::GetLinksRequest request;
            beebium::LinksState response;

            auto status = fixture.stub().GetLinks(&context, request, &response);
            REQUIRE(status.ok());
            CHECK(response.value() == 0xF8);
        }
    }

    SECTION("rejects values > 255") {
        grpc::ClientContext context;
        beebium::SetLinksRequest request;
        request.set_value(256);
        beebium::SetLinksResponse response;

        auto status = fixture.stub().SetLinks(&context, request, &response);
        REQUIRE(status.ok());
        CHECK_FALSE(response.success());
        CHECK(response.error() == "value must be 0-255");
    }
}

TEST_CASE("KeyboardService SetStartupScreenMode and GetStartupScreenMode roundtrip", "[grpc][keyboard][links]") {
    KeyboardTestFixture fixture;

    SECTION("default mode is 7") {
        grpc::ClientContext context;
        beebium::GetStartupScreenModeRequest request;
        beebium::StartupScreenModeState response;

        auto status = fixture.stub().GetStartupScreenMode(&context, request, &response);

        REQUIRE(status.ok());
        CHECK(response.mode() == 7);
    }

    SECTION("set mode 0 and read back") {
        {
            grpc::ClientContext context;
            beebium::SetStartupScreenModeRequest request;
            request.set_mode(0);
            beebium::SetStartupScreenModeResponse response;

            auto status = fixture.stub().SetStartupScreenMode(&context, request, &response);
            REQUIRE(status.ok());
            CHECK(response.success());
        }

        {
            grpc::ClientContext context;
            beebium::GetStartupScreenModeRequest request;
            beebium::StartupScreenModeState response;

            auto status = fixture.stub().GetStartupScreenMode(&context, request, &response);
            REQUIRE(status.ok());
            CHECK(response.mode() == 0);
        }
    }

    SECTION("set each mode 0-7") {
        for (uint32_t mode = 0; mode <= 7; ++mode) {
            {
                grpc::ClientContext context;
                beebium::SetStartupScreenModeRequest request;
                request.set_mode(mode);
                beebium::SetStartupScreenModeResponse response;

                auto status = fixture.stub().SetStartupScreenMode(&context, request, &response);
                REQUIRE(status.ok());
                CHECK(response.success());
            }

            {
                grpc::ClientContext context;
                beebium::GetStartupScreenModeRequest request;
                beebium::StartupScreenModeState response;

                auto status = fixture.stub().GetStartupScreenMode(&context, request, &response);
                REQUIRE(status.ok());
                CHECK(response.mode() == mode);
            }
        }
    }

    SECTION("rejects mode > 7") {
        grpc::ClientContext context;
        beebium::SetStartupScreenModeRequest request;
        request.set_mode(8);
        beebium::SetStartupScreenModeResponse response;

        auto status = fixture.stub().SetStartupScreenMode(&context, request, &response);
        REQUIRE(status.ok());
        CHECK_FALSE(response.success());
        CHECK(response.error() == "mode must be 0-7");
    }
}

TEST_CASE("KeyboardService SetStartupAutoBoot and GetStartupAutoBoot roundtrip", "[grpc][keyboard][links]") {
    KeyboardTestFixture fixture;

    SECTION("default auto-boot is false") {
        grpc::ClientContext context;
        beebium::GetStartupAutoBootRequest request;
        beebium::StartupAutoBootState response;

        auto status = fixture.stub().GetStartupAutoBoot(&context, request, &response);

        REQUIRE(status.ok());
        CHECK_FALSE(response.enabled());
    }

    SECTION("enable auto-boot and read back") {
        {
            grpc::ClientContext context;
            beebium::SetStartupAutoBootRequest request;
            request.set_enabled(true);
            beebium::SetStartupAutoBootResponse response;

            auto status = fixture.stub().SetStartupAutoBoot(&context, request, &response);
            REQUIRE(status.ok());
            CHECK(response.success());
        }

        {
            grpc::ClientContext context;
            beebium::GetStartupAutoBootRequest request;
            beebium::StartupAutoBootState response;

            auto status = fixture.stub().GetStartupAutoBoot(&context, request, &response);
            REQUIRE(status.ok());
            CHECK(response.enabled());
        }
    }

    SECTION("disable auto-boot after enabling") {
        // Enable
        {
            grpc::ClientContext context;
            beebium::SetStartupAutoBootRequest request;
            request.set_enabled(true);
            beebium::SetStartupAutoBootResponse response;
            fixture.stub().SetStartupAutoBoot(&context, request, &response);
        }

        // Disable
        {
            grpc::ClientContext context;
            beebium::SetStartupAutoBootRequest request;
            request.set_enabled(false);
            beebium::SetStartupAutoBootResponse response;

            auto status = fixture.stub().SetStartupAutoBoot(&context, request, &response);
            REQUIRE(status.ok());
            CHECK(response.success());
        }

        // Verify disabled
        {
            grpc::ClientContext context;
            beebium::GetStartupAutoBootRequest request;
            beebium::StartupAutoBootState response;

            auto status = fixture.stub().GetStartupAutoBoot(&context, request, &response);
            REQUIRE(status.ok());
            CHECK_FALSE(response.enabled());
        }
    }
}

TEST_CASE("KeyboardService semantic options to raw links roundtrip", "[grpc][keyboard][links]") {
    KeyboardTestFixture fixture;

    SECTION("mode 0 sets raw byte bits 0-2 to 0") {
        {
            grpc::ClientContext context;
            beebium::SetStartupScreenModeRequest request;
            request.set_mode(0);
            beebium::SetStartupScreenModeResponse response;
            fixture.stub().SetStartupScreenMode(&context, request, &response);
        }

        {
            grpc::ClientContext context;
            beebium::GetLinksRequest request;
            beebium::LinksState response;
            fixture.stub().GetLinks(&context, request, &response);

            // Default is 0xFF, mode 0 clears bits 0-2 -> 0xF8
            CHECK(response.value() == 0xF8);
        }
    }

    SECTION("mode 7 sets raw byte bits 0-2 to 7") {
        // First set to mode 0
        {
            grpc::ClientContext context;
            beebium::SetStartupScreenModeRequest request;
            request.set_mode(0);
            beebium::SetStartupScreenModeResponse response;
            fixture.stub().SetStartupScreenMode(&context, request, &response);
        }

        // Then set to mode 7
        {
            grpc::ClientContext context;
            beebium::SetStartupScreenModeRequest request;
            request.set_mode(7);
            beebium::SetStartupScreenModeResponse response;
            fixture.stub().SetStartupScreenMode(&context, request, &response);
        }

        {
            grpc::ClientContext context;
            beebium::GetLinksRequest request;
            beebium::LinksState response;
            fixture.stub().GetLinks(&context, request, &response);

            // bits 0-2 = 7, other bits unchanged at 0xF8 -> 0xFF
            CHECK(response.value() == 0xFF);
        }
    }

    SECTION("auto-boot enabled clears bit 3") {
        {
            grpc::ClientContext context;
            beebium::SetStartupAutoBootRequest request;
            request.set_enabled(true);
            beebium::SetStartupAutoBootResponse response;
            fixture.stub().SetStartupAutoBoot(&context, request, &response);
        }

        {
            grpc::ClientContext context;
            beebium::GetLinksRequest request;
            beebium::LinksState response;
            fixture.stub().GetLinks(&context, request, &response);

            // Default 0xFF with bit 3 cleared -> 0xF7
            CHECK(response.value() == 0xF7);
        }
    }

    SECTION("combined mode and auto-boot") {
        // Set mode 3
        {
            grpc::ClientContext context;
            beebium::SetStartupScreenModeRequest request;
            request.set_mode(3);
            beebium::SetStartupScreenModeResponse response;
            fixture.stub().SetStartupScreenMode(&context, request, &response);
        }

        // Enable auto-boot
        {
            grpc::ClientContext context;
            beebium::SetStartupAutoBootRequest request;
            request.set_enabled(true);
            beebium::SetStartupAutoBootResponse response;
            fixture.stub().SetStartupAutoBoot(&context, request, &response);
        }

        {
            grpc::ClientContext context;
            beebium::GetLinksRequest request;
            beebium::LinksState response;
            fixture.stub().GetLinks(&context, request, &response);

            // bits 0-2 = 3, bit 3 = 0, bits 4-7 = 0xF -> 0xF3
            CHECK(response.value() == 0xF3);
        }
    }
}

TEST_CASE("KeyboardService raw links to semantic options roundtrip", "[grpc][keyboard][links]") {
    KeyboardTestFixture fixture;

    SECTION("raw byte 0xF8 reads as mode 0") {
        {
            grpc::ClientContext context;
            beebium::SetLinksRequest request;
            request.set_value(0xF8);
            beebium::SetLinksResponse response;
            fixture.stub().SetLinks(&context, request, &response);
        }

        {
            grpc::ClientContext context;
            beebium::GetStartupScreenModeRequest request;
            beebium::StartupScreenModeState response;
            fixture.stub().GetStartupScreenMode(&context, request, &response);

            CHECK(response.mode() == 0);
        }
    }

    SECTION("raw byte 0xF7 reads as auto-boot enabled") {
        {
            grpc::ClientContext context;
            beebium::SetLinksRequest request;
            request.set_value(0xF7);  // bit 3 cleared
            beebium::SetLinksResponse response;
            fixture.stub().SetLinks(&context, request, &response);
        }

        {
            grpc::ClientContext context;
            beebium::GetStartupAutoBootRequest request;
            beebium::StartupAutoBootState response;
            fixture.stub().GetStartupAutoBoot(&context, request, &response);

            CHECK(response.enabled());
        }
    }

    SECTION("raw byte 0x00 reads as mode 0 and auto-boot enabled") {
        {
            grpc::ClientContext context;
            beebium::SetLinksRequest request;
            request.set_value(0x00);  // All bits cleared
            beebium::SetLinksResponse response;
            fixture.stub().SetLinks(&context, request, &response);
        }

        {
            grpc::ClientContext context;
            beebium::GetStartupScreenModeRequest request;
            beebium::StartupScreenModeState response;
            fixture.stub().GetStartupScreenMode(&context, request, &response);

            CHECK(response.mode() == 0);
        }

        {
            grpc::ClientContext context;
            beebium::GetStartupAutoBootRequest request;
            beebium::StartupAutoBootState response;
            fixture.stub().GetStartupAutoBoot(&context, request, &response);

            CHECK(response.enabled());
        }
    }

    SECTION("each mode value 0-7 roundtrips through raw byte") {
        for (uint32_t mode = 0; mode <= 7; ++mode) {
            // Set raw byte with mode in bits 0-2, other bits set
            uint8_t raw_value = 0xF8 | static_cast<uint8_t>(mode);
            {
                grpc::ClientContext context;
                beebium::SetLinksRequest request;
                request.set_value(raw_value);
                beebium::SetLinksResponse response;
                fixture.stub().SetLinks(&context, request, &response);
            }

            {
                grpc::ClientContext context;
                beebium::GetStartupScreenModeRequest request;
                beebium::StartupScreenModeState response;
                fixture.stub().GetStartupScreenMode(&context, request, &response);

                CHECK(response.mode() == mode);
            }
        }
    }
}

TEST_CASE("KeyboardService typing status says whether the machine is running",
          "[grpc][keyboard][typing]") {
    KeyboardTestFixture fixture;
    auto machine_running = [&] {
        grpc::ClientContext context;
        beebium::TypingStatus status;
        REQUIRE(fixture.stub()
                    .GetTypingStatus(&context, beebium::GetTypingStatusRequest{}, &status)
                    .ok());
        return status.machine_running();
    };

    CHECK(machine_running());
    fixture.machine().pause();
    CHECK_FALSE(machine_running());
    fixture.machine().resume();
    CHECK(machine_running());
}

TEST_CASE("KeyboardService WatchTypingStatus pushes when the machine stops",
          "[grpc][keyboard][typing]") {
    KeyboardTestFixture fixture;
    grpc::ClientContext context;
    beebium::WatchTypingStatusRequest request;
    request.set_min_interval_ms(5);
    auto reader = fixture.stub().WatchTypingStatus(&context, request);

    beebium::TypingStatus status;
    REQUIRE(reader->Read(&status));
    CHECK(status.machine_running());

    // The queue does not change; the stop alone is pushed.
    fixture.machine().pause();
    REQUIRE(reader->Read(&status));
    CHECK_FALSE(status.machine_running());
    CHECK(status.idle());

    fixture.machine().resume();
    REQUIRE(reader->Read(&status));
    CHECK(status.machine_running());

    context.TryCancel();
    while (reader->Read(&status)) {
    }
    (void)reader->Finish();
}

TEST_CASE("KeyboardService typing status says when the guest is ready for input",
          "[grpc][keyboard][typing]") {
#ifndef BEEBIUM_ROM_DIR
    SKIP("needs the MOS and BASIC ROMs");
#else
    KeyboardTestFixture fixture;
    auto guest_ready = [&] {
        grpc::ClientContext context;
        beebium::TypingStatus status;
        REQUIRE(fixture.stub()
                    .GetTypingStatus(&context, beebium::GetTypingStatusRequest{}, &status)
                    .ok());
        return status.guest_ready();
    };
    constexpr uint64_t half_a_second = 1'000'000;

    // Power-on: not ready while the MOS resets, ready once BASIC asks for a line.
    CHECK_FALSE(guest_ready());
    fixture.machine().run(half_a_second);
    CHECK(guest_ready());

    // Break: not ready while held, nor during the MOS's reset after it.
    fixture.machine().break_down();
    CHECK_FALSE(guest_ready());
    fixture.machine().run(1000);
    fixture.machine().break_up();
    CHECK_FALSE(guest_ready());
    fixture.machine().run(half_a_second);
    CHECK(guest_ready());
#endif
}

TEST_CASE("KeyboardService typing status counts cycles since the last reset",
          "[grpc][keyboard][typing]") {
    KeyboardTestFixture fixture;
    auto status = [&] {
        grpc::ClientContext context;
        beebium::TypingStatus typing;
        REQUIRE(fixture.stub()
                    .GetTypingStatus(&context, beebium::GetTypingStatusRequest{}, &typing)
                    .ok());
        return typing;
    };

    const auto at_power_on = status().cycles_since_reset();
    fixture.machine().run(5000);
    CHECK(status().cycles_since_reset() >= at_power_on + 5000);

    // Break pressed starts the count again: the guest's wait for readiness
    // begins there.
    fixture.machine().break_down();
    CHECK(status().cycles_since_reset() == 0);
    fixture.machine().run(1000);
    CHECK(status().cycles_since_reset() >= 1000);
    fixture.machine().break_up();
    CHECK(status().cycles_since_reset() == 0);
}

TEST_CASE("KeyboardService WatchTypingStatus pushes the count while the guest is not ready",
          "[grpc][keyboard][typing]") {
    KeyboardTestFixture fixture;
    fixture.machine().break_down();  // not ready, and the count runs from here
    grpc::ClientContext context;
    beebium::WatchTypingStatusRequest request;
    request.set_min_interval_ms(5);
    auto reader = fixture.stub().WatchTypingStatus(&context, request);

    beebium::TypingStatus first;
    REQUIRE(reader->Read(&first));
    REQUIRE_FALSE(first.guest_ready());

    // Nothing in the queue changes; time passing alone is pushed.
    fixture.machine().run(1000);
    beebium::TypingStatus later;
    REQUIRE(reader->Read(&later));
    CHECK(later.cycles_since_reset() > first.cycles_since_reset());

    context.TryCancel();
    beebium::TypingStatus drained;
    while (reader->Read(&drained)) {
    }
    (void)reader->Finish();
}
