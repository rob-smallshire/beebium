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

// Each frame carries the emulated cycle at which it completed (#111): the
// VideoRenderer stamps every vsync rising edge it delivers, and the
// FrameRenderer matches the stamps to the edges it sees.

#include <catch2/catch_test_macros.hpp>

#include "beebium/FrameBuffer.hpp"
#include "beebium/FrameRenderer.hpp"
#include "beebium/Machines.hpp"
#include "beebium/OutputQueue.hpp"
#include "beebium/PixelBatch.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

using namespace beebium;

namespace {

PixelBatch make_batch(uint8_t flags) {
    PixelBatch batch{};
    batch.clear();
    batch.set_type(PixelBatchType::Bitmap);
    batch.set_flags(flags);
    return batch;
}

// One field: a displayed line, then a vsync (whose rising edge completes it).
void feed_field(FrameRenderer& renderer) {
    renderer.process_unit(make_batch(VIDEO_FLAG_DISPLAY));
    renderer.process_unit(make_batch(VIDEO_FLAG_HSYNC));
    renderer.process_unit(make_batch(VIDEO_FLAG_VSYNC));
    renderer.process_unit(make_batch(VIDEO_FLAG_NONE));
}

}  // namespace

TEST_CASE("FrameRenderer stamps each frame with its vsync edge's cycle", "[video][frame][cycle]") {
    FrameBuffer frame_buffer;
    FrameRenderer renderer(&frame_buffer);
    OutputQueue<FieldCycle> field_cycles(16);
    renderer.set_field_cycles(&field_cycles);

    field_cycles.push({0, 1000});
    field_cycles.push({1, 40936});
    field_cycles.push({2, 80872});

    feed_field(renderer);
    CHECK(frame_buffer.metadata().cycle_count == 1000);
    feed_field(renderer);
    CHECK(frame_buffer.metadata().cycle_count == 40936);
    feed_field(renderer);
    CHECK(frame_buffer.metadata().cycle_count == 80872);
}

TEST_CASE("FrameRenderer reads a missing stamp as 0 and stays matched", "[video][frame][cycle]") {
    FrameBuffer frame_buffer;
    FrameRenderer renderer(&frame_buffer);
    OutputQueue<FieldCycle> field_cycles(16);
    renderer.set_field_cycles(&field_cycles);

    // Edge 1's stamp was never published (a full stamp ring).
    field_cycles.push({0, 1000});
    field_cycles.push({2, 80872});

    feed_field(renderer);
    CHECK(frame_buffer.metadata().cycle_count == 1000);
    feed_field(renderer);
    CHECK(frame_buffer.metadata().cycle_count == 0);
    feed_field(renderer);
    CHECK(frame_buffer.metadata().cycle_count == 80872);
}

TEST_CASE("FrameRenderer without a stamp source leaves cycle_count 0", "[video][frame][cycle]") {
    FrameBuffer frame_buffer;
    FrameRenderer renderer(&frame_buffer);
    feed_field(renderer);
    CHECK(frame_buffer.metadata().cycle_count == 0);
}

#ifdef BEEBIUM_ROM_DIR

namespace {

std::vector<uint8_t> load_rom(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    REQUIRE(file);
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

struct SeenFrame {
    uint64_t stamp;            // the frame's cycle_count
    uint64_t cycle_when_seen;  // machine cycle when the renderer produced it
};

constexpr uint64_t kChunkCycles = 1000;

// Boot a Model B into MODE 1 (about a second), optionally write CRTC R8, then
// record every new frame's stamp for the next second.
std::vector<SeenFrame> mode1_frames(std::optional<uint8_t> crtc_r8 = std::nullopt) {
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic = load_rom(rom_dirpath / "bbc-basic_2.rom");

    ModelB machine;
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());
    machine.memory().enable_video_output();
    machine.memory().set_screen_mode(1);
    machine.reset();

    FrameBuffer frame_buffer;
    FrameRenderer renderer(&frame_buffer);
    renderer.set_field_cycles(&machine.video_binding().renderer.field_cycles());
    auto& video_output = *machine.memory().video_output;

    auto run_chunk = [&] {
        machine.run(kChunkCycles);
        while (renderer.process(video_output, 100000) > 0) {}
    };

    while (machine.cycle_count() < 2'000'000) run_chunk();
    REQUIRE_FALSE(machine.memory().video_ula.teletext_mode());
    if (crtc_r8) {
        machine.write(0xFE00, 8);
        machine.write(0xFE01, *crtc_r8);
        // Let the field in progress under the old setting finish.
        for (int i = 0; i < 100; ++i) run_chunk();
    }

    std::vector<SeenFrame> frames;
    uint64_t last_version = frame_buffer.version();
    const uint64_t end = machine.cycle_count() + 1'000'000;
    while (machine.cycle_count() < end) {
        run_chunk();
        if (frame_buffer.version() != last_version) {
            last_version = frame_buffer.version();
            frames.push_back({frame_buffer.metadata().cycle_count, machine.cycle_count()});
        }
    }
    return frames;
}

void check_stamped_when_completed(const std::vector<SeenFrame>& frames) {
    REQUIRE(frames.size() >= 20);
    for (size_t i = 0; i < frames.size(); ++i) {
        INFO("frame " << i);
        CHECK(frames[i].stamp != 0);
        // A frame is stamped when it completes: within the chunk that
        // produced it (a chunk may overrun by up to one instruction).
        CHECK(frames[i].stamp <= frames[i].cycle_when_seen);
        CHECK(frames[i].stamp + kChunkCycles + 7 > frames[i].cycle_when_seen);
    }
}

}  // namespace

// The MOS runs the 6845 in interlace-sync mode in MODES 0-6 (R8=1, the *TV
// default), so a field is 312.5 lines of 128 cycles: 40,000 cycles, with the
// vsync of alternate fields half a line later. The stamps land within a cycle
// of that, and every consecutive pair of fields spans exactly 80,000 cycles.
TEST_CASE("MODE 1 frames are stamped one field (312.5 lines x 128 cycles) apart",
          "[video][frame][cycle][mode1]") {
    const auto frames = mode1_frames();
    check_stamped_when_completed(frames);
    for (size_t i = 1; i < frames.size(); ++i) {
        INFO("frame " << i);
        const uint64_t field = frames[i].stamp - frames[i - 1].stamp;
        CHECK(field >= 40'000 - 1);
        CHECK(field <= 40'000 + 1);
        if (i > 1) {
            CHECK(frames[i].stamp - frames[i - 2].stamp == 80'000);
        }
    }
}

// With interlace sync off (*TV ,1 writes R8=0) every field is 312 lines.
TEST_CASE("MODE 1 without interlace sync stamps frames 312 x 128 cycles apart",
          "[video][frame][cycle][mode1]") {
    const auto frames = mode1_frames(uint8_t{0});
    check_stamped_when_completed(frames);
    for (size_t i = 1; i < frames.size(); ++i) {
        INFO("frame " << i);
        CHECK(frames[i].stamp - frames[i - 1].stamp == 312 * 128);
    }
}

#endif
