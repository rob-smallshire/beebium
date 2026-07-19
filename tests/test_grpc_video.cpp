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
        auto mos = load_rom(std::string(BEEBIUM_ROM_DIR) + "/acorn-mos_1_20.rom");
        auto basic = load_rom(std::string(BEEBIUM_ROM_DIR) + "/bbc-basic_2.rom");
        std::copy(mos.begin(), mos.end(), machine_.state().memory.mos_rom.data());
        machine_.state().memory.load_basic(basic.data(), basic.size());
#endif

        // Enable video output
        machine_.state().memory.enable_video_output();
        machine_.reset();

        // Start server on a dynamically allocated port
        server_ = std::make_unique<beebium::service::Server<beebium::ModelB>>(machine_, "127.0.0.1", 0);
        server_->start({}, {});

        // Create client channel using the actual bound port
        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
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
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
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

// Count bright pixels in a frame (BGRA32 format)
static size_t count_bright_pixels_grpc(const std::string& pixels) {
    size_t count = 0;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(pixels.data());
    size_t num_pixels = pixels.size() / 4;

    for (size_t i = 0; i < num_pixels; ++i) {
        // BGRA format
        uint8_t b = data[i * 4 + 0];
        uint8_t g = data[i * 4 + 1];
        uint8_t r = data[i * 4 + 2];

        // Count as bright if luminance > 128
        int luminance = (r + g + b) / 3;
        if (luminance > 128) {
            ++count;
        }
    }
    return count;
}

TEST_CASE("VideoService save streamed frames", "[grpc][video][cursor][.save]") {
    // Save streamed frames to disk for visual inspection

    VideoTestFixture fixture;

    // Boot to BASIC prompt
    for (uint64_t i = 0; i < 3'000'000; ++i) {
        fixture.machine().step();
        if (fixture.machine().read(0x7C28) == 'B' &&
            fixture.machine().read(0x7C29) == 'B') {
            break;
        }
    }

    // Let display stabilize
    fixture.run_cycles(500000);

    grpc::ClientContext context;
    beebium::SubscribeFramesRequest request;

    auto reader = fixture.stub().SubscribeFrames(&context, request);

    // Run emulation in background
    std::atomic<bool> running{true};
    std::thread emu_thread([&]() {
        while (running) {
            fixture.run_cycles(80000);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    // Save 100 frames to disk
    constexpr int NUM_FRAMES = 100;
    for (int i = 0; i < NUM_FRAMES; ++i) {
        beebium::Frame frame;
        if (!reader->Read(&frame)) {
            break;
        }

        // Save as PPM
        std::string filename = "/tmp/grpc_frame_" + std::to_string(i) + ".ppm";
        std::ofstream file(filename);
        if (file) {
            int width = frame.width();
            int height = frame.height();
            file << "P3\n" << width << " " << height << "\n255\n";

            const uint8_t* data = reinterpret_cast<const uint8_t*>(frame.pixels().data());
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    size_t idx = (y * width + x) * 4;
                    int b = data[idx + 0];
                    int g = data[idx + 1];
                    int r = data[idx + 2];
                    file << r << " " << g << " " << b << " ";
                }
                file << "\n";
            }
        }

        // Report brightness
        size_t bright = count_bright_pixels_grpc(frame.pixels());
        if (i >= 50) {  // Only report stable frames
            WARN("Frame " << i << ": bright=" << bright << " saved to " << filename);
        }
    }

    running = false;
    context.TryCancel();
    emu_thread.join();
}

TEST_CASE("VideoService streams cursor blink pattern", "[grpc][video][cursor]") {
    // Verify cursor blink is visible in streamed frames
    // This tests the full pipeline: core -> framebuffer -> grpc -> client

    // Create machine and boot BEFORE creating server
    // (Server contains FrameRenderer which needs clean state after CRTC is programmed)
    beebium::ModelB machine;
#ifdef BEEBIUM_ROM_DIR
    auto mos = load_rom(std::string(BEEBIUM_ROM_DIR) + "/acorn-mos_1_20.rom");
    auto basic = load_rom(std::string(BEEBIUM_ROM_DIR) + "/bbc-basic_2.rom");
    std::copy(mos.begin(), mos.end(), machine.state().memory.mos_rom.data());
    machine.state().memory.load_basic(basic.data(), basic.size());
#endif
    machine.state().memory.enable_video_output();
    machine.reset();

    // Boot to BASIC prompt
    for (uint64_t i = 0; i < 3'000'000; ++i) {
        machine.step();
        if (machine.read(0x7C28) == 'B' && machine.read(0x7C29) == 'B') {
            break;
        }
    }

    // Let display stabilize
    machine.run(200000);

    // Drain any pending video output from boot phase
    if (machine.state().memory.video_output.has_value()) {
        auto& queue = machine.state().memory.video_output.value();
        queue.consume(queue.size());
    }

    // Now create server with fresh video state
    beebium::service::Server<beebium::ModelB> server(machine, "127.0.0.1", 0);
    server.start({}, {});

    std::string address = "127.0.0.1:" + std::to_string(server.port());
    auto channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
    auto stub = beebium::VideoService::NewStub(channel);

    grpc::ClientContext context;
    beebium::SubscribeFramesRequest request;

    auto reader = stub->SubscribeFrames(&context, request);

    // Run emulation and collect frame brightness
    std::atomic<bool> running{true};
    std::vector<size_t> brightness_values;
    std::mutex brightness_mutex;

    std::thread emu_thread([&]() {
        while (running) {
            machine.run(80000);  // One frame worth
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    // Receive 100 frames and measure brightness
    constexpr int NUM_FRAMES = 100;
    for (int i = 0; i < NUM_FRAMES; ++i) {
        beebium::Frame frame;
        if (!reader->Read(&frame)) {
            break;
        }
        size_t bright = count_bright_pixels_grpc(frame.pixels());
        std::lock_guard<std::mutex> lock(brightness_mutex);
        brightness_values.push_back(bright);
    }

    // Stop emulation
    running = false;
    context.TryCancel();
    emu_thread.join();

    REQUIRE(brightness_values.size() >= 50);

    // Analyze brightness for periodic pattern (cursor blink)
    // Skip first 20 frames for boot/stabilization
    std::vector<size_t> stable_values(brightness_values.begin() + 20, brightness_values.end());

    size_t min_val = *std::min_element(stable_values.begin(), stable_values.end());
    size_t max_val = *std::max_element(stable_values.begin(), stable_values.end());
    size_t range = max_val - min_val;

    INFO("Streamed frames: min=" << min_val << " max=" << max_val << " range=" << range);

    // With cursor blinking, expect ~16 pixel variation
    // If range is 0, cursor is not visible in streamed frames
    CHECK(range >= 10);

    server.stop();
}

// ============================================================================
// Screen text
// ============================================================================

namespace {

// Run the machine until frames have been rendered and the bands recorded.
//
// The renderer runs on the server's own thread, pulling batches the emulation
// thread pushed, so both have to make progress before there is a frame to read
// geometry from.
// Runs the machine on its own thread for as long as it is alive.
//
// Paced rather than run flat out: the batch queue is bounded, and a producer
// that gets too far ahead has its batches dropped, taking with them the sync
// flags that end a frame. Pacing is also what lets the server's render thread
// keep up, which it must for there to be a completed frame to read at all.
class RunningMachine {
public:
    explicit RunningMachine(VideoTestFixture& fixture)
        : thread_([this, &fixture]() {
              while (running_) {
                  fixture.run_cycles(20000);
                  std::this_thread::sleep_for(std::chrono::milliseconds(1));
              }
          }) {}

    ~RunningMachine() {
        running_ = false;
        thread_.join();
    }

private:
    std::atomic<bool> running_{true};
    std::thread thread_;
};

// Poll until the display has produced something, or give up.
//
// A machine that has just been reset has not drawn anything yet, and a poll
// beats a fixed sleep: it ends as soon as there is something to read rather
// than always costing the worst case.
bool wait_for_bands(VideoTestFixture& fixture,
                    std::chrono::milliseconds timeout = std::chrono::seconds(10)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        grpc::ClientContext context;
        beebium::GetScreenGeometryRequest request;
        beebium::ScreenGeometry response;
        if (fixture.stub().GetScreenGeometry(&context, request, &response).ok()
            && response.bands_size() > 0) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

} // anonymous namespace

TEST_CASE("VideoService GetScreenGeometry reports a band's character grid",
          "[grpc][video][screen-text]") {
    VideoTestFixture fixture;
    RunningMachine running(fixture);
    REQUIRE(wait_for_bands(fixture));

    grpc::ClientContext context;
    beebium::GetScreenGeometryRequest request;
    beebium::ScreenGeometry response;

    auto status = fixture.stub().GetScreenGeometry(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.bands_size() >= 1);

    // Whatever the mode, a band has a grid: snapping is about where the cells
    // are, which is a separate question from what is in them.
    for (const auto& band : response.bands()) {
        CHECK(band.bottom() > band.top());
        CHECK(band.cell_width() > 0);
        CHECK(band.cell_height() > 0);
        CHECK(band.column_pitch() >= band.cell_width());
        CHECK(band.row_pitch() >= band.cell_height());
    }
}

TEST_CASE("VideoService GetScreenText reads a MODE 7 boot screen",
          "[grpc][video][screen-text]") {
    VideoTestFixture fixture;
    RunningMachine running(fixture);
    REQUIRE(wait_for_bands(fixture));

    grpc::ClientContext context;
    beebium::GetScreenTextRequest request;
    beebium::ScreenText response;

    auto status = fixture.stub().GetScreenText(&context, request, &response);

    REQUIRE(status.ok());

    // The teletext strategy reads exact character codes, so nothing it returns
    // can be uncertain.
    CHECK(response.unreadable_cells() == 0);
    CHECK(response.ambiguous_cells() == 0);

    if (response.supported()) {
        CHECK(response.runs_size() > 0);
        // Lines are joined with LF; a platform-native ending is the client's
        // business.
        CHECK(response.text().find('\r') == std::string::npos);
    }
}

TEST_CASE("VideoService GetScreenText honours a region",
          "[grpc][video][screen-text]") {
    VideoTestFixture fixture;
    RunningMachine running(fixture);
    REQUIRE(wait_for_bands(fixture));

    grpc::ClientContext context;
    beebium::GetScreenTextRequest request;
    request.mutable_region()->set_x(0);
    request.mutable_region()->set_y(0);
    request.mutable_region()->set_width(120);
    request.mutable_region()->set_height(40);
    beebium::ScreenText response;

    auto status = fixture.stub().GetScreenText(&context, request, &response);

    REQUIRE(status.ok());

    // Every run reported must lie inside what was asked for.
    for (const auto& run : response.runs()) {
        CHECK(run.bounds().x() + run.bounds().width() <= 120 + run.cell_width());
        CHECK(run.bounds().y() < 40 + run.cell_height());
    }
}
