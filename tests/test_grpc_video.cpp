// Test gRPC VideoService
//
// These tests verify the VideoService implementation by acting as a gRPC client.
// They create a local server, connect to it, and verify frame streaming works correctly.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include "beebium/Machines.hpp"
#include "beebium/service/Server.hpp"

#include "video.grpc.pb.h"
#include <grpcpp/grpcpp.h>

#include <thread>
#include <chrono>
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
class VideoTestFixture {
public:
    VideoTestFixture() {
        // Load ROMs
#ifdef BEEBIUM_ROM_DIR
        auto mos = load_rom(std::string(BEEBIUM_ROM_DIR) + "/os12.rom");
        auto basic = load_rom(std::string(BEEBIUM_ROM_DIR) + "/basic2.rom");
        std::copy(mos.begin(), mos.end(), machine_.state().memory.mos_rom.data());
        std::copy(basic.begin(), basic.end(), machine_.state().memory.basic_rom.data());
#endif

        // Enable video output
        machine_.state().memory.enable_video_output();
        machine_.reset();

        // Start server on a random available port
        server_ = std::make_unique<beebium::service::Server>(machine_, "127.0.0.1", 0);
        // Note: Port 0 would need special handling to get actual port
        // For now, use a fixed test port
        server_ = std::make_unique<beebium::service::Server>(machine_, "127.0.0.1", 50052);
        server_->start();

        // Create client channel
        channel_ = grpc::CreateChannel("127.0.0.1:50052",
                                       grpc::InsecureChannelCredentials());
        stub_ = beebium::VideoService::NewStub(channel_);
    }

    ~VideoTestFixture() {
        server_->stop();
    }

    beebium::ModelB& machine() { return machine_; }
    beebium::VideoService::Stub& stub() { return *stub_; }

    void run_cycles(uint64_t cycles) {
        machine_.run(cycles);
    }

private:
    beebium::ModelB machine_;
    std::unique_ptr<beebium::service::Server> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::VideoService::Stub> stub_;
};

} // anonymous namespace

TEST_CASE("VideoService GetConfig returns video dimensions", "[grpc][video]") {
    VideoTestFixture fixture;

    grpc::ClientContext context;
    beebium::GetConfigRequest request;
    beebium::VideoConfig response;

    auto status = fixture.stub().GetConfig(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.width() == 736);   // Default frame width (includes overscan)
    CHECK(response.height() == 576);  // Default frame height (interlaced)
    CHECK(response.framerate_hz() == 50);
}

TEST_CASE("VideoService SubscribeFrames streams frames", "[grpc][video]") {
    VideoTestFixture fixture;

    grpc::ClientContext context;
    beebium::SubscribeFramesRequest request;

    auto reader = fixture.stub().SubscribeFrames(&context, request);

    // Run emulation in a separate thread to generate frames
    std::atomic<bool> running{true};
    std::thread emu_thread([&]() {
        while (running) {
            fixture.run_cycles(20000);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    // Try to receive at least one frame
    beebium::Frame frame;
    bool received = reader->Read(&frame);

    // Stop emulation
    running = false;
    context.TryCancel();
    emu_thread.join();

    REQUIRE(received);
    CHECK(frame.width() == 736);
    CHECK(frame.height() == 576);
    CHECK(frame.pixels().size() == 736 * 576 * 4);  // BGRA32
}

TEST_CASE("VideoService frame version increments on VSYNC", "[grpc][video]") {
    VideoTestFixture fixture;

    grpc::ClientContext context;
    beebium::SubscribeFramesRequest request;

    auto reader = fixture.stub().SubscribeFrames(&context, request);

    // Run emulation to generate multiple frames
    std::atomic<bool> running{true};
    std::thread emu_thread([&]() {
        while (running) {
            fixture.run_cycles(20000);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    // Receive two frames and check version increments
    beebium::Frame frame1, frame2;
    bool received1 = reader->Read(&frame1);
    bool received2 = reader->Read(&frame2);

    // Stop emulation
    running = false;
    context.TryCancel();
    emu_thread.join();

    REQUIRE(received1);
    REQUIRE(received2);
    CHECK(frame2.frame_number() > frame1.frame_number());
}
