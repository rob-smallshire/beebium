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

#include <atomic>
#include <thread>
#include <chrono>
#include <fstream>
#include <functional>
#include <string>
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

// Emulated cycles per second (the 6502's 2 MHz).
constexpr uint64_t kCyclesPerSecond = 2'000'000;

// The cycles in one step of a running machine: about half a field.
constexpr uint64_t kStepCycles = 20000;

// How long a machine runs, in emulated time, before a test waiting on it gives
// up. Generous: a test meets its condition long before, on any host, because a
// slow host slows the machine and the renderer together.
constexpr uint64_t kEmulatedBudgetCycles = 10 * kCyclesPerSecond;

// How long the server's render thread may take to consume one step's batches
// before it is judged stalled. Only a render thread that has stopped reaches
// it; a slow one merely slows the machine paced on it.
constexpr auto kRenderStallBackstop = std::chrono::seconds(120);

// Wait until the server's render thread has consumed every batch the machine
// has produced. Pacing the machine on this rather than on a sleep means it
// never outruns the renderer however loaded the host is: a batch dropped from a
// full queue takes the sync flags that end a frame with it. False if the
// backstop passes first.
bool wait_for_renderer(beebium::ModelB& machine) {
    const auto& queue = machine.state().memory.video_output.value();
    const auto backstop = std::chrono::steady_clock::now() + kRenderStallBackstop;
    while (!queue.empty()) {
        if (std::chrono::steady_clock::now() >= backstop) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    return true;
}

// Runs the fixture's machine on its own thread, a step at a time, each step
// paced on the server's render thread consuming it, until it is stopped, its
// emulated budget is spent, or the render thread stalls. `on_finish` runs when
// it stops of its own accord, so a test blocked on a stream can be released.
class RunningMachine {
public:
    explicit RunningMachine(VideoTestFixture& fixture,
                            std::function<void()> on_finish = {},
                            uint64_t budget_cycles = kEmulatedBudgetCycles)
        : fixture_(fixture),
          on_finish_(std::move(on_finish)),
          budget_cycles_(budget_cycles),
          thread_([this]() { run(); }) {}

    ~RunningMachine() { stop(); }

    // Stop the machine. Afterwards its cycle count may be read directly.
    void stop() {
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    // The machine's cycle count after its latest step.
    uint64_t cycles() const { return cycles_.load(); }

    // Why the machine stopped of its own accord, or empty if it did not.
    std::string finish_reason() const {
        switch (finish_.load()) {
            case Finish::BudgetSpent:
                return "the machine ran its budget of "
                       + std::to_string(budget_cycles_ / kCyclesPerSecond)
                       + " emulated seconds";
            case Finish::RenderStalled:
                return "the server's render thread consumed nothing for "
                       + std::to_string(kRenderStallBackstop.count()) + " s";
            case Finish::None:
                break;
        }
        return {};
    }

private:
    enum class Finish { None, BudgetSpent, RenderStalled };

    void run() {
        const uint64_t start = fixture_.machine().cycle_count();
        while (running_) {
            fixture_.run_cycles(kStepCycles);
            cycles_ = fixture_.machine().cycle_count();
            if (!wait_for_renderer(fixture_.machine())) {
                finish(Finish::RenderStalled);
                return;
            }
            if (cycles_ - start >= budget_cycles_) {
                finish(Finish::BudgetSpent);
                return;
            }
        }
    }

    void finish(Finish why) {
        finish_ = why;
        if (on_finish_) {
            on_finish_();
        }
    }

    VideoTestFixture& fixture_;
    std::function<void()> on_finish_;
    const uint64_t budget_cycles_;
    std::atomic<bool> running_{true};
    std::atomic<uint64_t> cycles_{0};
    std::atomic<Finish> finish_{Finish::None};
    std::thread thread_;  // last, so it starts once the members above exist
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
    RunningMachine running(fixture, [&]() { context.TryCancel(); });

    // Try to receive at least one frame
    beebium::Frame frame;
    bool received = reader->Read(&frame);

    // Stop emulation
    running.stop();
    context.TryCancel();

    INFO(running.finish_reason());
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
    RunningMachine running(fixture, [&]() { context.TryCancel(); });

    // Receive two frames and check version increments
    beebium::Frame frame1, frame2;
    bool received1 = reader->Read(&frame1);
    bool received2 = reader->Read(&frame2);

    // Stop emulation
    running.stop();
    context.TryCancel();

    INFO(running.finish_reason());
    REQUIRE(received1);
    REQUIRE(received2);
    CHECK(frame2.frame_number() > frame1.frame_number());
}

TEST_CASE("VideoService frames carry the cycle at which they completed", "[grpc][video][cycle]") {
    VideoTestFixture fixture;

    // The first frame completes at cycle 0, on the vsync edge the CRTC raises
    // at power-on, and a stamp of 0 also means "unknown". Run past it before
    // subscribing, so the stream cannot start with that one frame.
    fixture.run_cycles(kStepCycles);
    REQUIRE(wait_for_renderer(fixture.machine()));

    grpc::ClientContext context;
    beebium::SubscribeFramesRequest request;
    auto reader = fixture.stub().SubscribeFrames(&context, request);

    RunningMachine running(fixture, [&]() { context.TryCancel(); });

    std::vector<beebium::Frame> frames(4);
    bool received = true;
    for (auto& frame : frames) {
        received = received && reader->Read(&frame);
    }

    running.stop();
    context.TryCancel();
    // Stopped, so this is the last cycle the machine reached: no frame can
    // have completed after it.
    const uint64_t final_cycle = fixture.machine().cycle_count();

    INFO(running.finish_reason());
    REQUIRE(received);
    for (size_t i = 0; i < frames.size(); ++i) {
        INFO("frame " << i << ": number " << frames[i].frame_number()
             << ", cycle " << frames[i].cycle_count());
        CHECK(frames[i].cycle_count() != 0);
        if (i > 0) {
            CHECK(frames[i].frame_number() > frames[i - 1].frame_number());
            CHECK(frames[i].cycle_count() > frames[i - 1].cycle_count());
        }
    }
    CHECK(frames.back().cycle_count() <= final_cycle);
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

    std::atomic<bool> render_stalled{false};
    std::thread emu_thread([&]() {
        while (running) {
            machine.run(80000);  // One frame worth
            // Let the renderer consume the frame before the next, so none of
            // its batches is dropped however loaded the host is.
            if (!wait_for_renderer(machine)) {
                render_stalled = true;
                context.TryCancel();
                return;
            }
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

    REQUIRE_FALSE(render_stalled);
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

// Wait, while the machine runs, until the display has produced bands to read
// geometry from. Returns empty when it has, or why it never did.
//
// The renderer runs on the server's own thread, pulling batches the emulation
// thread pushed, so both have to make progress before there is a frame to read
// geometry from; the running machine is paced on the renderer, and its budget
// bounds the wait in emulated time.
std::string wait_for_bands(VideoTestFixture& fixture, const RunningMachine& running) {
    auto has_bands = [&fixture]() {
        grpc::ClientContext context;
        beebium::GetScreenGeometryRequest request;
        beebium::ScreenGeometry response;
        return fixture.stub().GetScreenGeometry(&context, request, &response).ok()
               && response.bands_size() > 0;
    };
    while (!has_bands()) {
        if (const std::string reason = running.finish_reason(); !reason.empty()) {
            return has_bands() ? std::string{} : "no bands: " + reason;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return {};
}

} // anonymous namespace

TEST_CASE("VideoService GetScreenGeometry reports a band's character grid",
          "[grpc][video][screen-text]") {
    VideoTestFixture fixture;
    RunningMachine running(fixture);
    REQUIRE(wait_for_bands(fixture, running) == std::string{});

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
    REQUIRE(wait_for_bands(fixture, running) == std::string{});

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
    REQUIRE(wait_for_bands(fixture, running) == std::string{});

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

// ============================================================================
// Holding a screen
// ============================================================================
//
// A reading depends on four things that move independently -- the pixels, the
// band geometry, the teletext grid and the font in RAM. Read at four different
// instants they describe a screen that never existed, and on a moving display
// the text a user copies is not the text they selected. Holding captures them
// together so later reads name one still.

namespace {

// Run the machine a step at a time, letting the server's render thread consume
// each step, until a predicate holds. Returns empty when it does, or why it
// never did: the emulated budget spent, or the render thread stalled.
//
// Single-threaded on purpose, unlike RunningMachine above: these tests write to
// screen memory between steps, and a machine running on its own thread would
// race with that.
template <typename Predicate>
std::string pump_until(VideoTestFixture& fixture, Predicate predicate) {
    const uint64_t start = fixture.machine().cycle_count();
    while (!predicate()) {
        if (fixture.machine().cycle_count() - start >= kEmulatedBudgetCycles) {
            return "not seen within "
                   + std::to_string(kEmulatedBudgetCycles / kCyclesPerSecond)
                   + " emulated seconds";
        }
        fixture.run_cycles(kStepCycles);
        if (!wait_for_renderer(fixture.machine())) {
            return "the server's render thread consumed nothing for "
                   + std::to_string(kRenderStallBackstop.count()) + " s";
        }
    }
    return {};
}

grpc::Status read_screen_text(VideoTestFixture& fixture,
                              beebium::ScreenText& out,
                              const uint64_t* hold_id = nullptr) {
    grpc::ClientContext context;
    beebium::GetScreenTextRequest request;
    if (hold_id != nullptr) {
        request.set_hold_id(*hold_id);
    }
    return fixture.stub().GetScreenText(&context, request, &out);
}

std::string live_text(VideoTestFixture& fixture) {
    beebium::ScreenText response;
    if (!read_screen_text(fixture, response).ok()) {
        return {};
    }
    return response.text();
}

grpc::Status hold_screen(VideoTestFixture& fixture, beebium::ScreenHold& out,
                         bool include_frame = false) {
    grpc::ClientContext context;
    beebium::HoldScreenRequest request;
    request.set_include_frame(include_frame);
    return fixture.stub().HoldScreen(&context, request, &out);
}

// Write a marker into MODE 7 screen memory, where the boot screen's first row
// is blank and the MOS will not overwrite it.
void write_marker(VideoTestFixture& fixture, const std::string& marker) {
    for (size_t i = 0; i < marker.size(); ++i) {
        fixture.machine().write(static_cast<uint16_t>(0x7C00 + i),
                                static_cast<uint8_t>(marker[i]));
    }
}

} // anonymous namespace

TEST_CASE("VideoService HoldScreen returns the grid with the hold",
          "[grpc][video][screen-hold]") {
    VideoTestFixture fixture;
    REQUIRE(pump_until(fixture, [&] { return !live_text(fixture).empty(); }) == std::string{});

    beebium::ScreenHold hold;
    auto status = hold_screen(fixture, hold);

    REQUIRE(status.ok());
    CHECK(hold.hold_id() != 0);

    // The grid comes back with the hold, so a drag can snap without a second
    // call -- and so the geometry cannot drift from the pixels it describes.
    CHECK(hold.geometry().bands_size() >= 1);
    CHECK(hold.geometry().bands(0).cell_width() > 0);

    // The still is only sent when asked for.
    CHECK_FALSE(hold.has_frame());
}

TEST_CASE("VideoService a held screen does not change as the machine draws on",
          "[grpc][video][screen-hold]") {
    VideoTestFixture fixture;
    REQUIRE(pump_until(fixture, [&] {
        return live_text(fixture).find("BASIC") != std::string::npos;
    }) == std::string{});

    beebium::ScreenHold hold;
    REQUIRE(hold_screen(fixture, hold).ok());

    const uint64_t held_id = hold.hold_id();
    beebium::ScreenText at_hold;
    REQUIRE(read_screen_text(fixture, at_hold, &held_id).ok());

    // Change the screen under the hold, and wait for the live reading to show
    // it -- so the two readings are known to differ before either is judged.
    write_marker(fixture, "ZZZZ");
    REQUIRE(pump_until(fixture, [&] {
        return live_text(fixture).find("ZZZZ") != std::string::npos;
    }) == std::string{});

    beebium::ScreenText live;
    REQUIRE(read_screen_text(fixture, live).ok());
    beebium::ScreenText held;
    REQUIRE(read_screen_text(fixture, held, &held_id).ok());

    // The live screen has the marker; the held one is the still it was taken
    // from and never will.
    CHECK(live.text().find("ZZZZ") != std::string::npos);
    CHECK(held.text().find("ZZZZ") == std::string::npos);
    CHECK(held.text() == at_hold.text());

    // Pinned to one frame, however far the machine has run since.
    CHECK(held.frame_number() == at_hold.frame_number());
    CHECK(live.frame_number() > held.frame_number());
}

TEST_CASE("VideoService GetScreenGeometry reads a held screen",
          "[grpc][video][screen-hold]") {
    VideoTestFixture fixture;
    REQUIRE(pump_until(fixture, [&] { return !live_text(fixture).empty(); }) == std::string{});

    beebium::ScreenHold hold;
    REQUIRE(hold_screen(fixture, hold).ok());

    grpc::ClientContext context;
    beebium::GetScreenGeometryRequest request;
    request.set_hold_id(hold.hold_id());
    beebium::ScreenGeometry response;

    REQUIRE(fixture.stub().GetScreenGeometry(&context, request, &response).ok());

    // The same grid the hold reported, from the same capture.
    REQUIRE(response.bands_size() == hold.geometry().bands_size());
    CHECK(response.bands(0).cell_width() == hold.geometry().bands(0).cell_width());
    CHECK(response.bands(0).row_pitch() == hold.geometry().bands(0).row_pitch());
    CHECK(response.frame_number() == hold.geometry().frame_number());
}

TEST_CASE("VideoService a released hold is gone", "[grpc][video][screen-hold]") {
    VideoTestFixture fixture;
    REQUIRE(pump_until(fixture, [&] { return !live_text(fixture).empty(); }) == std::string{});

    beebium::ScreenHold hold;
    REQUIRE(hold_screen(fixture, hold).ok());

    grpc::ClientContext release_context;
    beebium::ReleaseScreenRequest release;
    release.set_hold_id(hold.hold_id());
    beebium::ReleaseScreenResponse released;
    REQUIRE(fixture.stub().ReleaseScreen(&release_context, release, &released).ok());

    const uint64_t released_id = hold.hold_id();
    beebium::ScreenText response;
    auto status = read_screen_text(fixture, response, &released_id);

    CHECK(status.error_code() == grpc::StatusCode::NOT_FOUND);
}

TEST_CASE("VideoService an unknown hold is refused, not read live",
          "[grpc][video][screen-hold]") {
    // Falling back to the live screen would be the very confusion holding
    // exists to prevent, and it would be silent.
    VideoTestFixture fixture;
    REQUIRE(pump_until(fixture, [&] { return !live_text(fixture).empty(); }) == std::string{});

    const uint64_t never_held = 0xDEADBEEF;

    beebium::ScreenText text;
    CHECK(read_screen_text(fixture, text, &never_held).error_code()
          == grpc::StatusCode::NOT_FOUND);

    grpc::ClientContext context;
    beebium::GetScreenGeometryRequest request;
    request.set_hold_id(never_held);
    beebium::ScreenGeometry geometry;
    CHECK(fixture.stub().GetScreenGeometry(&context, request, &geometry).error_code()
          == grpc::StatusCode::NOT_FOUND);
}

TEST_CASE("VideoService HoldScreen can return the still it captured",
          "[grpc][video][screen-hold]") {
    // So a client can display exactly the frame its reads will be made
    // against, rather than whichever frame it last happened to draw.
    VideoTestFixture fixture;
    REQUIRE(pump_until(fixture, [&] { return !live_text(fixture).empty(); }) == std::string{});

    beebium::ScreenHold hold;
    REQUIRE(hold_screen(fixture, hold, /*include_frame=*/true).ok());

    REQUIRE(hold.has_frame());
    const auto& frame = hold.frame();
    CHECK(frame.width() > 0);
    CHECK(frame.height() > 0);
    CHECK(frame.pixels().size()
          == static_cast<size_t>(frame.width()) * frame.height() * 4);

    // The still and the reading are the same frame, which is the whole point.
    const uint64_t still_id = hold.hold_id();
    beebium::ScreenText held;
    REQUIRE(read_screen_text(fixture, held, &still_id).ok());
    CHECK(held.frame_number() == frame.frame_number());
}

TEST_CASE("VideoService CaptureFrame returns a frame completed at or after a cycle",
          "[grpc][video][cycle]") {
    VideoTestFixture fixture;
    RunningMachine running(fixture);

    // Ask repeatedly for frames completed after a cycle the machine has not
    // reached yet: each must be stamped at or after it, never before. The
    // timeout is the most the server allows, as a backstop only: the machine
    // reaches each cycle in a few steps, however slowly the host runs it.
    std::vector<std::string> failures;
    std::vector<uint64_t> asked, stamped;
    for (int i = 0; i < 5; ++i) {
        const uint64_t after = running.cycles() + 50000;
        grpc::ClientContext ctx;
        beebium::CaptureFrameRequest req;
        req.set_after_cycle(after);
        req.set_timeout_ms(60000);
        beebium::Frame frame;
        auto status = fixture.stub().CaptureFrame(&ctx, req, &frame);
        if (!status.ok()) {
            failures.push_back(status.error_message() + " (machine at cycle "
                               + std::to_string(running.cycles()) + ")");
        }
        asked.push_back(after);
        stamped.push_back(frame.cycle_count());
    }

    running.stop();

    INFO(running.finish_reason());
    for (const auto& failure : failures) {
        INFO(failure);
    }
    REQUIRE(failures.empty());
    for (size_t i = 0; i < asked.size(); ++i) {
        INFO("capture " << i);
        CHECK(stamped[i] >= asked[i]);
        // Frames complete every field (~40,000 cycles, or 80,000 for an
        // interlaced MODE 7 frame), so the first one at or after the cycle
        // is well within two frames of it.
        CHECK(stamped[i] < asked[i] + 2 * 80000 + 20000);
    }
}

TEST_CASE("VideoService CaptureFrame gives up at its deadline on a stopped machine",
          "[grpc][video][cycle]") {
    VideoTestFixture fixture;
    fixture.run_cycles(200000);  // some frames, then the machine stands still

    grpc::ClientContext ctx;
    beebium::CaptureFrameRequest req;
    req.set_after_cycle(fixture.machine().cycle_count() + 1'000'000);
    req.set_timeout_ms(200);
    beebium::Frame frame;
    const auto start = std::chrono::steady_clock::now();
    auto status = fixture.stub().CaptureFrame(&ctx, req, &frame);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK(status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED);
    CHECK(elapsed >= std::chrono::milliseconds(200));
    CHECK(elapsed < std::chrono::milliseconds(2000));
}

TEST_CASE("VideoService CaptureFrame with after_cycle 0 returns the current frame",
          "[grpc][video][cycle]") {
    VideoTestFixture fixture;
    fixture.run_cycles(200000);
    // Let the render thread consume the batches, so there is a frame.
    REQUIRE(wait_for_renderer(fixture.machine()));

    grpc::ClientContext ctx;
    beebium::CaptureFrameRequest req;
    req.set_timeout_ms(1000);
    beebium::Frame frame;
    REQUIRE(fixture.stub().CaptureFrame(&ctx, req, &frame).ok());
    CHECK(frame.cycle_count() != 0);
    CHECK(frame.cycle_count() <= fixture.machine().cycle_count());
    CHECK(frame.pixels().size() == static_cast<size_t>(frame.width()) * frame.height() * 4);
}
