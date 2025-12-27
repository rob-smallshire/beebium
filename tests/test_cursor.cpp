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

#include <catch2/catch_test_macros.hpp>
#include <beebium/Machines.hpp>
#include <beebium/FrameAllocator.hpp>
#include <beebium/FrameBuffer.hpp>
#include <beebium/FrameRenderer.hpp>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace beebium;

namespace {

// Load a ROM file into a vector
std::vector<uint8_t> load_rom(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open ROM: " + filepath.string());
    }

    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);

    return data;
}

#ifdef BEEBIUM_ROM_DIR
// Check if ROM files exist
bool roms_available() {
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    return std::filesystem::exists(rom_dirpath / "acorn-mos_1_20.rom") &&
           std::filesystem::exists(rom_dirpath / "bbc-basic_2.rom");
}
#endif

// Count white/bright pixels in framebuffer
size_t count_bright_pixels(const std::span<const uint32_t> frame) {
    size_t count = 0;
    for (auto pixel : frame) {
        uint8_t r = (pixel >> 16) & 0xFF;
        uint8_t g = (pixel >> 8) & 0xFF;
        uint8_t b = pixel & 0xFF;

        // Count as bright if luminance > 128
        int luminance = (r + g + b) / 3;
        if (luminance > 128) {
            ++count;
        }
    }
    return count;
}

} // anonymous namespace

#ifdef BEEBIUM_ROM_DIR

TEST_CASE("Mode 7 cursor blinks", "[video][cursor][integration]") {
    REQUIRE(roms_available());

    // Load ROMs
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    // Create machine with video output
    ModelB machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.reset();

    // Create video pipeline
    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);  // Full PAL resolution
    FrameRenderer renderer(&fb);
    fb.clear(0);

    // Boot detection: look for "BBC Computer" at screen address 0x7C28
    const uint16_t check_addr = 0x7C28;
    const char* expected_str = "BBC Computer";
    const size_t expected_len = 12;

    // Run boot sequence with periodic video drain
    constexpr uint64_t max_cycles = 3'000'000;  // ~1.5 seconds at 2MHz
    constexpr uint64_t drain_interval = 10'000;

    bool boot_complete = false;
    uint64_t drain_counter = 0;

    while (machine.cycle_count() < max_cycles && !boot_complete) {
        machine.step();
        ++drain_counter;

        // Periodically drain video queue to renderer
        if (drain_counter >= drain_interval) {
            drain_counter = 0;
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }

        // Check for boot completion
        bool matches = true;
        for (size_t i = 0; i < expected_len && matches; ++i) {
            if (machine.read(check_addr + i) != static_cast<uint8_t>(expected_str[i])) {
                matches = false;
            }
        }
        boot_complete = matches;
    }

    REQUIRE(boot_complete);
    INFO("Boot completed at cycle " << machine.cycle_count());
    REQUIRE(machine.memory().video_ula.teletext_mode());

    // After boot, capture multiple frames and measure bright pixel counts
    // The cursor blinks at 1/16 or 1/32 field rate (typically 32 frames = 16 on, 16 off)
    // We'll capture 64 frames to ensure we see at least one full blink cycle

    constexpr int NUM_FRAMES = 64;
    constexpr int CYCLES_PER_FRAME = 40000;  // ~1 frame at 2MHz/50Hz

    std::vector<size_t> bright_counts;
    bright_counts.reserve(NUM_FRAMES);

    for (int frame_idx = 0; frame_idx < NUM_FRAMES; ++frame_idx) {
        // Clear and reset renderer for fresh frame
        fb.clear(0);
        renderer.reset();

        // Drain old queue data
        if (machine.memory().video_output.has_value()) {
            auto& queue = machine.memory().video_output.value();
            queue.consume(queue.size());
        }

        // Run one frame worth of cycles
        for (int i = 0; i < CYCLES_PER_FRAME; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }

        // Force swap and count bright pixels
        fb.swap();
        auto frame = fb.read_frame();
        size_t bright = count_bright_pixels(frame);
        bright_counts.push_back(bright);
    }

    // Analyze bright pixel counts for oscillation
    // The cursor should cause the count to vary between frames

    // Find min and max bright pixel counts
    size_t min_bright = *std::min_element(bright_counts.begin(), bright_counts.end());
    size_t max_bright = *std::max_element(bright_counts.begin(), bright_counts.end());

    INFO("Min bright pixels: " << min_bright);
    INFO("Max bright pixels: " << max_bright);

    // The cursor XOR should cause a visible difference in bright pixel count
    // When cursor is on, it inverts a character cell (12x20 pixels = 240 pixels)
    // However, it's XORing, so:
    // - Black cells become white (adds bright pixels)
    // - White cells become black (removes bright pixels)
    // At the BASIC prompt, cursor is on a black background, so cursor ON adds bright pixels

    // Check that there's variation (cursor is blinking)
    size_t variation = max_bright - min_bright;
    INFO("Bright pixel variation: " << variation);

    // We expect at least ~100 pixels difference when cursor toggles
    // (Mode 7 character is 12x20 = 240 pixels, but with antialiasing and partial coverage,
    // the actual difference might be less)
    CHECK(variation >= 50);

    // Count transitions (frames where bright count changes significantly)
    int transitions = 0;
    constexpr size_t THRESHOLD = 30;  // Minimum change to count as transition

    for (size_t i = 1; i < bright_counts.size(); ++i) {
        size_t diff = (bright_counts[i] > bright_counts[i-1]) ?
                      (bright_counts[i] - bright_counts[i-1]) :
                      (bright_counts[i-1] - bright_counts[i]);
        if (diff >= THRESHOLD) {
            ++transitions;
        }
    }

    INFO("Number of transitions: " << transitions);

    // With 64 frames and cursor blinking at 1/16 or 1/32 rate:
    // - 1/16 rate: ~4 transitions (every 16 frames)
    // - 1/32 rate: ~2 transitions (every 32 frames)
    // We should see at least 1 transition in 64 frames
    CHECK(transitions >= 1);
}

TEST_CASE("CRTC cursor signal is generated", "[video][cursor]") {
    REQUIRE(roms_available());

    // Load ROMs
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    // Create machine with video output
    ModelB machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.reset();

    // Boot to BASIC prompt
    const uint16_t check_addr = 0x7C28;
    const char* expected_str = "BBC Computer";
    bool boot_complete = false;

    for (uint64_t i = 0; i < 3'000'000 && !boot_complete; ++i) {
        machine.step();

        bool matches = true;
        for (size_t j = 0; expected_str[j] && matches; ++j) {
            if (machine.read(check_addr + j) != static_cast<uint8_t>(expected_str[j])) {
                matches = false;
            }
        }
        boot_complete = matches;
    }
    REQUIRE(boot_complete);

    // After boot, verify CRTC is configured for cursor display
    // Read cursor start register (R10) to check blink mode
    machine.memory().crtc.write(0, 10);  // Select R10
    uint8_t cursor_start = machine.memory().crtc.read(1);

    // Bits 5-6 of R10 control blink mode:
    // 00 = steady (no blink)
    // 01 = no cursor
    // 10 = blink at 1/16 field rate
    // 11 = blink at 1/32 field rate
    uint8_t blink_mode = (cursor_start >> 5) & 0x03;
    INFO("CRTC R10 = " << (int)cursor_start << ", blink mode = " << (int)blink_mode);

    // The MOS typically sets up blinking cursor (mode 2 or 3)
    // Mode 1 (no cursor) would be unexpected at BASIC prompt
    CHECK(blink_mode != 1);  // Should not be "no cursor"

    // Read cursor position registers
    machine.memory().crtc.write(0, 14);  // Select R14 (cursor addr high)
    uint8_t cursor_high = machine.memory().crtc.read(1);
    machine.memory().crtc.write(0, 15);  // Select R15 (cursor addr low)
    uint8_t cursor_low = machine.memory().crtc.read(1);

    uint16_t cursor_addr = (static_cast<uint16_t>(cursor_high) << 8) | cursor_low;
    INFO("Cursor address = " << std::hex << cursor_addr);

    // Cursor should be positioned somewhere in screen memory
    // The CRTC cursor address is a 14-bit linear address that wraps
    // At the BASIC prompt, cursor should be non-zero (not at origin)
    CHECK(cursor_addr > 0);
    CHECK(cursor_addr < 0x4000);  // 14-bit max
}

#endif // BEEBIUM_ROM_DIR
