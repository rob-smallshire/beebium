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
        auto mos = load_rom(std::string(BEEBIUM_ROM_DIR) + "/os12.rom");
        auto basic = load_rom(std::string(BEEBIUM_ROM_DIR) + "/basic2.rom");
        std::copy(mos.begin(), mos.end(), machine_.state().memory.mos_rom.data());
        std::copy(basic.begin(), basic.end(), machine_.state().memory.basic_rom.data());
#endif
        machine_.reset();

        // Start server
        server_ = std::make_unique<beebium::service::Server>(machine_, "127.0.0.1", 50053);
        server_->start();

        // Create client channel
        channel_ = grpc::CreateChannel("127.0.0.1:50053",
                                       grpc::InsecureChannelCredentials());
        stub_ = beebium::KeyboardService::NewStub(channel_);
    }

    ~KeyboardTestFixture() {
        server_->stop();
    }

    beebium::ModelB& machine() { return machine_; }
    beebium::KeyboardService::Stub& stub() { return *stub_; }

private:
    beebium::ModelB machine_;
    std::unique_ptr<beebium::service::Server> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::KeyboardService::Stub> stub_;
};

} // anonymous namespace

TEST_CASE("KeyboardService KeyDown sets key in matrix", "[grpc][keyboard]") {
    KeyboardTestFixture fixture;

    // Press key at row 0, column 0 (should be 'Q' on BBC keyboard)
    grpc::ClientContext context;
    beebium::KeyRequest request;
    request.set_row(0);
    request.set_column(0);
    beebium::KeyResponse response;

    auto status = fixture.stub().KeyDown(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.accepted());

    // Verify key is pressed in the peripheral
    CHECK(fixture.machine().state().memory.system_via_peripheral.is_key_pressed(0, 0));
}

TEST_CASE("KeyboardService KeyUp clears key in matrix", "[grpc][keyboard]") {
    KeyboardTestFixture fixture;

    // First press a key
    {
        grpc::ClientContext context;
        beebium::KeyRequest request;
        request.set_row(1);
        request.set_column(2);
        beebium::KeyResponse response;
        fixture.stub().KeyDown(&context, request, &response);
    }

    // Verify it's pressed
    CHECK(fixture.machine().state().memory.system_via_peripheral.is_key_pressed(1, 2));

    // Now release it
    {
        grpc::ClientContext context;
        beebium::KeyRequest request;
        request.set_row(1);
        request.set_column(2);
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
    request.set_row(10);
    request.set_column(10);
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

TEST_CASE("KeyboardService multiple keys can be pressed simultaneously", "[grpc][keyboard]") {
    KeyboardTestFixture fixture;

    // Press SHIFT (row 0, column 0) and 'A' (row 4, column 1)
    {
        grpc::ClientContext context;
        beebium::KeyRequest request;
        request.set_row(0);
        request.set_column(0);
        beebium::KeyResponse response;
        fixture.stub().KeyDown(&context, request, &response);
    }
    {
        grpc::ClientContext context;
        beebium::KeyRequest request;
        request.set_row(4);
        request.set_column(1);
        beebium::KeyResponse response;
        fixture.stub().KeyDown(&context, request, &response);
    }

    // Both should be pressed
    auto& peripheral = fixture.machine().state().memory.system_via_peripheral;
    CHECK(peripheral.is_key_pressed(0, 0));
    CHECK(peripheral.is_key_pressed(4, 1));
}
