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
#include <beebium/Saa5050.hpp>
#include <filesystem>
#include <fstream>
#include <vector>
#include <map>
#include <set>

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

    // Boot detection: look for "BBC Computer" at screen address 0x7C28
    const uint16_t check_addr = 0x7C28;
    const char* expected_str = "BBC Computer";
    const size_t expected_len = 12;

    // Boot without video processing (CRTC is in degenerate state during boot)
    constexpr uint64_t max_cycles = 3'000'000;
    bool boot_complete = false;

    while (machine.cycle_count() < max_cycles && !boot_complete) {
        machine.step();

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

    // Run extra stabilization cycles after boot
    for (int i = 0; i < 200000; ++i) {
        machine.step();
    }

    // Drain any pending video output from boot phase
    if (machine.memory().video_output.has_value()) {
        auto& queue = machine.memory().video_output.value();
        queue.consume(queue.size());
    }

    // Create video pipeline after boot (with fresh state)
    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);  // Full PAL resolution
    FrameRenderer renderer(&fb);
    fb.clear(0);

    // After boot, capture multiple frames and measure bright pixel counts
    // The cursor blinks at 1/16 or 1/32 field rate (typically 32 frames = 16 on, 16 off)
    // We'll capture 64 frames to ensure we see at least one full blink cycle

    constexpr int NUM_FRAMES = 64;
    constexpr uint64_t MAX_CYCLES_PER_FRAME = 200000;  // Safety limit

    std::vector<size_t> bright_counts;
    bright_counts.reserve(NUM_FRAMES);

    for (int frame_idx = 0; frame_idx < NUM_FRAMES; ++frame_idx) {
        // Wait for next complete frame by watching version counter
        uint64_t start_version = fb.version();
        uint64_t cycles_this_frame = 0;

        while (fb.version() == start_version && cycles_this_frame < MAX_CYCLES_PER_FRAME) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
            ++cycles_this_frame;
        }

        // Count bright pixels in the completed frame
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

TEST_CASE("Cursor visible in rendered frame", "[video][cursor][failing]") {
    // Boot, wait for stable display, then check for cursor blink pattern
    // This test should FAIL if cursor is not blinking

    REQUIRE(roms_available());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    ModelB machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Boot to BASIC prompt with continuous rendering
    for (uint64_t i = 0; i < 3'000'000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        if (machine.read(0x7C28) == 'B' && machine.read(0x7C29) == 'B') {
            break;
        }
    }

    // Run to stabilize - let natural vsync handle frame swaps
    for (int i = 0; i < 500000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    // Capture frame brightness
    std::vector<size_t> frame_brightness;
    for (int frame = 0; frame < 64; ++frame) {
        // Run one frame worth of cycles (~80000 for Mode 7)
        for (int i = 0; i < 80000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }

        // Read current frame brightness (don't swap or clear)
        auto pixels = fb.read_frame();
        size_t bright = count_bright_pixels(pixels);
        frame_brightness.push_back(bright);
    }

    // Exclude initial unstable frames, look at last 48 frames
    std::vector<size_t> stable_frames(frame_brightness.begin() + 16, frame_brightness.end());

    size_t min_bright = *std::min_element(stable_frames.begin(), stable_frames.end());
    size_t max_bright = *std::max_element(stable_frames.begin(), stable_frames.end());
    size_t range = max_bright - min_bright;

    INFO("Stable frames brightness range: " << range);
    INFO("Min: " << min_bright << ", Max: " << max_bright);

    // Log stable frame values
    std::string values;
    for (size_t i = 0; i < stable_frames.size(); ++i) {
        values += std::to_string(stable_frames[i]) + " ";
        if ((i + 1) % 8 == 0) values += "\n  ";
    }
    WARN("Stable frame brightness:\n  " << values);

    // With cursor blinking, expect ~16 pixel difference (underline cursor)
    // If no blink, difference will be 0
    CHECK(range >= 10);
}

TEST_CASE("Cursor pixels appear in video output", "[video][cursor]") {
    // This test verifies cursor XOR is actually applied to pixel output
    // It should FAIL if cursor is not being rendered

    REQUIRE(roms_available());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    ModelB machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.reset();

    // Boot to BASIC prompt
    for (uint64_t i = 0; i < 3'000'000; ++i) {
        machine.step();
        if (machine.read(0x7C28) == 'B' && machine.read(0x7C29) == 'B') {
            break;
        }
    }

    // Run extra cycles to ensure display is stable
    for (int i = 0; i < 200000; ++i) {
        machine.step();
    }

    // Drain any pending video output
    if (machine.memory().video_output.has_value()) {
        auto& queue = machine.memory().video_output.value();
        queue.consume(queue.size());
    }

    // Set cursor to steady mode (no blinking) for easier detection
    // R10 bits 5-6: 00 = steady, 01 = no cursor, 10 = blink 1/16, 11 = blink 1/32
    machine.memory().crtc.write(0, 10);  // Select R10
    uint8_t r10 = machine.memory().crtc.read(1);
    INFO("Original R10 = " << static_cast<int>(r10));

    // Force steady cursor by clearing bits 5-6
    machine.memory().crtc.write(0, 10);
    machine.memory().crtc.write(1, r10 & 0x1F);  // Clear blink mode bits

    // Count pixels with cursor XOR applied
    // When cursor is active, pixels should be inverted (XOR with 0x0FFF)
    // We'll count how many PixelBatches have non-black pixels that could be cursor

    uint64_t total_batches = 0;
    uint64_t batches_with_bright_pixels = 0;
    uint64_t max_brightness_in_batch = 0;

    // Render several frames and examine pixel output
    constexpr int NUM_FRAMES = 4;
    constexpr int CYCLES_PER_FRAME = 80000;

    for (int frame = 0; frame < NUM_FRAMES; ++frame) {
        for (int cycle = 0; cycle < CYCLES_PER_FRAME; ++cycle) {
            machine.step();
        }

        // Process all pixel batches from this frame
        if (machine.memory().video_output.has_value()) {
            auto& queue = machine.memory().video_output.value();
            auto buffers = queue.get_consumer_buffer();

            auto process_batch = [&](const PixelBatch& batch) {
                ++total_batches;
                uint64_t batch_brightness = 0;
                for (int i = 0; i < 8; ++i) {
                    auto pixel = batch.pixels.pixels[i];
                    // Sum RGB components
                    batch_brightness += pixel.bits.r + pixel.bits.g + pixel.bits.b;
                }
                if (batch_brightness > 0) {
                    ++batches_with_bright_pixels;
                }
                if (batch_brightness > max_brightness_in_batch) {
                    max_brightness_in_batch = batch_brightness;
                }
            };

            for (size_t i = 0; i < buffers.a.size(); ++i) {
                process_batch(buffers.a[i]);
            }
            for (size_t i = 0; i < buffers.b.size(); ++i) {
                process_batch(buffers.b[i]);
            }
            queue.consume(buffers.total());
        }
    }

    INFO("Total batches: " << total_batches);
    INFO("Batches with bright pixels: " << batches_with_bright_pixels);
    INFO("Max brightness in single batch: " << max_brightness_in_batch);

    // With the "BBC Computer 32K" banner and cursor, we should have many bright pixels
    // The cursor alone at the prompt position should contribute
    CHECK(batches_with_bright_pixels > 100);

    // Now check specifically for cursor by comparing frames with cursor on vs off
    // Set cursor to "no cursor" mode
    machine.memory().crtc.write(0, 10);
    machine.memory().crtc.write(1, (r10 & 0x1F) | 0x20);  // Mode 01 = no cursor

    // Drain queue
    if (machine.memory().video_output.has_value()) {
        machine.memory().video_output.value().consume(
            machine.memory().video_output.value().size());
    }

    uint64_t no_cursor_bright_batches = 0;
    for (int frame = 0; frame < NUM_FRAMES; ++frame) {
        for (int cycle = 0; cycle < CYCLES_PER_FRAME; ++cycle) {
            machine.step();
        }
        if (machine.memory().video_output.has_value()) {
            auto& queue = machine.memory().video_output.value();
            auto buffers = queue.get_consumer_buffer();

            for (size_t i = 0; i < buffers.a.size(); ++i) {
                uint64_t brightness = 0;
                for (int p = 0; p < 8; ++p) {
                    auto pixel = buffers.a[i].pixels.pixels[p];
                    brightness += pixel.bits.r + pixel.bits.g + pixel.bits.b;
                }
                if (brightness > 0) ++no_cursor_bright_batches;
            }
            for (size_t i = 0; i < buffers.b.size(); ++i) {
                uint64_t brightness = 0;
                for (int p = 0; p < 8; ++p) {
                    auto pixel = buffers.b[i].pixels.pixels[p];
                    brightness += pixel.bits.r + pixel.bits.g + pixel.bits.b;
                }
                if (brightness > 0) ++no_cursor_bright_batches;
            }
            queue.consume(buffers.total());
        }
    }

    INFO("Bright batches with cursor: " << batches_with_bright_pixels);
    INFO("Bright batches without cursor: " << no_cursor_bright_batches);

    // There should be MORE bright batches with cursor than without
    // because the cursor XORs a black area to white
    int64_t difference = static_cast<int64_t>(batches_with_bright_pixels) -
                         static_cast<int64_t>(no_cursor_bright_batches);
    INFO("Difference (cursor - no cursor): " << difference);

    // The cursor should add at least some bright batches
    // At the BASIC prompt, cursor is on black, so XOR makes it white
    CHECK(difference > 0);
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

TEST_CASE("Cursor displays on all text lines", "[video][cursor][diagnostic]") {
    // This test diagnoses a bug where cursor doesn't display on lines 1 and 6
    // but works on lines 2-5. It types characters to move cursor to different
    // lines and checks if cursor address matching occurs.

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
    INFO("Boot completed at cycle " << machine.cycle_count());

    // Create video pipeline for frame rendering
    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Data collection for cursor analysis
    struct CursorMatchData {
        std::set<uint8_t> rows_with_match;
        std::map<uint8_t, uint16_t> cursor_addr_by_row;
        std::map<uint8_t, uint16_t> char_addr_by_row;
        std::map<uint8_t, uint16_t> screen_start_by_row;
        uint64_t total_callbacks = 0;
        uint64_t total_matches = 0;
    };
    CursorMatchData cursor_data;

    // Install cursor debug callback
    machine.memory().crtc.set_cursor_debug_callback(
        [&cursor_data](uint16_t char_addr, uint16_t cursor_pos, uint16_t screen_start,
                       uint8_t row, uint8_t /*raster*/, bool cursor_detected) {
            ++cursor_data.total_callbacks;
            if (cursor_detected) {
                ++cursor_data.total_matches;
                cursor_data.rows_with_match.insert(row);
            }
            // Record addresses for each row (first occurrence)
            if (cursor_data.cursor_addr_by_row.find(row) == cursor_data.cursor_addr_by_row.end()) {
                cursor_data.cursor_addr_by_row[row] = cursor_pos;
                cursor_data.char_addr_by_row[row] = char_addr;
                cursor_data.screen_start_by_row[row] = screen_start;
            }
        }
    );

    // Function to run one frame and collect cursor data
    auto run_frame = [&machine, &fb, &renderer]() {
        fb.clear(0);
        renderer.reset();

        // Drain old queue
        if (machine.memory().video_output.has_value()) {
            auto& queue = machine.memory().video_output.value();
            queue.consume(queue.size());
        }

        // Run one frame
        // Mode 7 uses 1MHz CRTC clock, need more CPU cycles for full frame
        constexpr int CYCLES_PER_FRAME = 80000;
        for (int i = 0; i < CYCLES_PER_FRAME; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
        fb.swap(fb.metadata());
    };

    // Run multiple frames to ensure cursor detection happens across blink cycle
    constexpr int NUM_FRAMES = 64;

    INFO("Running " << NUM_FRAMES << " frames to detect cursor...");
    for (int frame = 0; frame < NUM_FRAMES; ++frame) {
        run_frame();
    }

    // Report results
    INFO("Total cursor debug callbacks: " << cursor_data.total_callbacks);
    INFO("Total cursor matches: " << cursor_data.total_matches);
    INFO("Rows with cursor match: " << cursor_data.rows_with_match.size());

    for (const auto& row : cursor_data.rows_with_match) {
        WARN("  Row " << static_cast<int>(row) << " had cursor match");
    }

    // Report CRTC state for each row observed
    WARN("CRTC state by row (first occurrence) - " << cursor_data.cursor_addr_by_row.size() << " rows:");
    for (const auto& [row, cursor_addr] : cursor_data.cursor_addr_by_row) {
        uint16_t char_addr = cursor_data.char_addr_by_row[row];
        uint16_t screen_start = cursor_data.screen_start_by_row[row];
        bool matched = cursor_data.rows_with_match.count(row) > 0;

        WARN("  Row " << static_cast<int>(row) << ": cursor=0x" << std::hex << cursor_addr
             << " char=0x" << char_addr << " start=0x" << screen_start
             << std::dec << (matched ? " [MATCH]" : " [NO MATCH]"));
    }

    // Check that we got at least some cursor matches
    REQUIRE(cursor_data.total_callbacks > 0);
    CHECK(cursor_data.total_matches > 0);

    // At the BASIC prompt, cursor should be on one specific row
    // (likely row 2 or so after the banner text)
    CHECK(cursor_data.rows_with_match.size() >= 1);

    // Clean up callback
    machine.memory().crtc.clear_cursor_debug_callback();
}

TEST_CASE("Cursor detection at multiple line positions", "[video][cursor][diagnostic]") {
    // More detailed test: Run machine, check CRTC registers, and verify
    // cursor address matches char_addr at some point during frame rendering

    REQUIRE(roms_available());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    ModelB machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.reset();

    // Boot to prompt
    for (uint64_t i = 0; i < 3'000'000; ++i) {
        machine.step();
        if (machine.read(0x7C28) == 'B' && machine.read(0x7C29) == 'B') {
            break;
        }
    }

    // Read CRTC cursor position
    auto read_cursor_position = [&machine]() -> uint16_t {
        machine.memory().crtc.write(0, 14);
        uint8_t high = machine.memory().crtc.read(1);
        machine.memory().crtc.write(0, 15);
        uint8_t low = machine.memory().crtc.read(1);
        return (static_cast<uint16_t>(high) << 8) | low;
    };

    // Read screen start address
    auto read_screen_start = [&machine]() -> uint16_t {
        machine.memory().crtc.write(0, 12);
        uint8_t high = machine.memory().crtc.read(1);
        machine.memory().crtc.write(0, 13);
        uint8_t low = machine.memory().crtc.read(1);
        return (static_cast<uint16_t>(high) << 8) | low;
    };

    uint16_t cursor_pos = read_cursor_position();
    uint16_t screen_start = read_screen_start();

    INFO("Initial cursor position: 0x" << std::hex << cursor_pos);
    INFO("Screen start address: 0x" << std::hex << screen_start);
    INFO("Cursor offset from start: " << std::dec << (cursor_pos - screen_start));

    // Track if cursor address ever matches during frame rendering
    bool cursor_ever_matched = false;
    uint64_t match_count = 0;

    machine.memory().crtc.set_cursor_debug_callback(
        [&cursor_ever_matched, &match_count](
            uint16_t /*char_addr*/, uint16_t /*cursor_pos*/, uint16_t /*screen_start*/,
            uint8_t /*row*/, uint8_t /*raster*/, bool cursor_detected) {
            if (cursor_detected) {
                cursor_ever_matched = true;
                ++match_count;
            }
        }
    );

    // Run 32 frames (enough for blink cycle)
    // Mode 7 uses 1MHz CRTC clock, need more CPU cycles for full frame
    constexpr int CYCLES_PER_FRAME = 80000;
    for (int frame = 0; frame < 32; ++frame) {
        for (int i = 0; i < CYCLES_PER_FRAME; ++i) {
            machine.step();
        }
    }

    INFO("Cursor matched " << match_count << " times");
    CHECK(cursor_ever_matched);
    CHECK(match_count > 100);  // Should match many times per frame

    machine.memory().crtc.clear_cursor_debug_callback();
}

TEST_CASE("Cursor at each line after boot", "[video][cursor][failing]") {
    // Test cursor detection at each line position as MOS sets it
    // Lines 5, 10, 16, 17, 22 fail in the client

    REQUIRE(roms_available());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    ModelB machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Boot to BASIC prompt
    for (uint64_t i = 0; i < 3'000'000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        if (machine.read(0x7C28) == 'B' && machine.read(0x7C29) == 'B') {
            break;
        }
    }

    // Stabilize
    for (int i = 0; i < 200000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    // Read initial cursor position (should be line 5, col 1 after ">")
    auto read_cursor = [&]() -> uint16_t {
        return machine.memory().crtc.cursor_position();
    };

    auto read_screen_start = [&]() -> uint16_t {
        return machine.memory().crtc.screen_start();
    };

    // Check cursor detection for current position
    // Cursor blinks at 1/16 field rate: 16 frames on, 16 frames off
    // Run 48 frames to ensure at least one full "on" period
    auto check_cursor_visible = [&](int expected_line) -> bool {
        uint64_t matches = 0;
        machine.memory().crtc.set_cursor_debug_callback(
            [&matches](uint16_t, uint16_t, uint16_t, uint8_t, uint8_t, bool detected) {
                if (detected) ++matches;
            }
        );

        // Run 48 frames (Mode 7 at 1MHz CRTC = 80000 cycles per frame)
        constexpr int NUM_FRAMES = 48;
        constexpr int CYCLES_PER_FRAME = 80000;
        for (int frame = 0; frame < NUM_FRAMES; ++frame) {
            for (int i = 0; i < CYCLES_PER_FRAME; ++i) {
                machine.step();
                if (machine.memory().video_output.has_value()) {
                    renderer.process(machine.memory().video_output.value());
                }
            }
        }

        machine.memory().crtc.clear_cursor_debug_callback();

        uint16_t cursor_pos = read_cursor();
        uint16_t screen_start = read_screen_start();
        uint16_t offset = cursor_pos - screen_start;
        int line = offset / 40;
        int col = offset % 40;

        WARN("Line " << expected_line << ": cursor=0x" << std::hex << cursor_pos
             << " start=0x" << screen_start << std::dec
             << " (line " << line << ", col " << col << ")"
             << " matches=" << matches
             << (matches > 0 ? " [OK]" : " [MISSING]"));

        return matches > 0;
    };

    // Check line 5 (initial prompt)
    bool line5_ok = check_cursor_visible(5);

    // Simulate pressing Enter by injecting newline via keyboard
    // For simplicity, directly write to screen and update cursor via OSWRCH
    auto press_enter = [&]() {
        // Inject CR (13) into keyboard buffer
        // The MOS keyboard buffer is at 0x03E0-0x03FF
        // Or we can call OSWRCH directly by setting up registers and JSR

        // Simpler: write CR to WRCHV input
        // Actually, let's just poke the VDU queue

        // Even simpler: directly manipulate cursor position by calling MOS routines
        // Set A=13, call OSWRCH at 0xFFEE

        // Store A=13
        machine.state().cpu.a = 13;
        // Set up return address on stack (we'll use a BRK at 0x0000)
        machine.state().cpu.s.b.l = 0xFD;
        machine.write(0x01FE, 0x00);  // Low byte of return-1
        machine.write(0x01FF, 0x00);  // High byte of return-1
        // Set PC to OSWRCH
        machine.state().cpu.pc.w = 0xFFEE;

        // Run until we hit the BRK (or a reasonable number of cycles)
        for (int i = 0; i < 50000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
            // Check if we're back at address 0 (BRK handler)
            if (machine.state().cpu.pc.w < 0x0100) {
                break;
            }
        }

        // Run more to let display update
        for (int i = 0; i < 100000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    };

    // Test lines 6-24
    std::vector<bool> results;
    results.push_back(line5_ok);

    for (int line = 6; line <= 24; ++line) {
        press_enter();
        results.push_back(check_cursor_visible(line));
    }

    // Report summary
    WARN("Summary:");
    int failures = 0;
    for (size_t i = 0; i < results.size(); ++i) {
        if (!results[i]) {
            WARN("  Line " << (5 + i) << ": FAILED");
            ++failures;
        }
    }

    CHECK(failures == 0);
}

// Helper to save framebuffer as raw BGRA file
void save_frame_raw(const std::string& filename, const std::span<const uint32_t>& pixels, int width, int height) {
    std::ofstream file(filename, std::ios::binary);
    if (!file) return;
    // Write header: width, height (as 4-byte ints)
    file.write(reinterpret_cast<const char*>(&width), 4);
    file.write(reinterpret_cast<const char*>(&height), 4);
    // Write pixels
    file.write(reinterpret_cast<const char*>(pixels.data()), pixels.size() * 4);
}

// Helper to save as PPM (portable bitmap format - easy to view)
void save_frame_ppm(const std::string& filename, const std::span<const uint32_t>& pixels, int width, int height) {
    std::ofstream file(filename);
    if (!file) return;
    file << "P3\n" << width << " " << height << "\n255\n";
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint32_t p = pixels[y * width + x];
            int r = (p >> 16) & 0xFF;
            int g = (p >> 8) & 0xFF;
            int b = p & 0xFF;
            file << r << " " << g << " " << b << " ";
        }
        file << "\n";
    }
}

TEST_CASE("Exact reproduction Enter key sequence", "[video][cursor][repro]") {
    // Exact reproduction of user scenario:
    // 1. Boot to BASIC prompt
    // 2. Save frame (initial cursor at line 5)
    // 3. Simulate Enter key presses and save frames at each line

    REQUIRE(roms_available());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    ModelB machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Boot with rendering
    for (uint64_t i = 0; i < 3'000'000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        if (machine.read(0x7C28) == 'B' && machine.read(0x7C29) == 'B') break;
    }

    // Stabilize
    for (int i = 0; i < 500000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    WARN("Boot complete. Now saving frames at each Enter press...");

    // Save initial frame (cursor at line 5)
    // Run enough frames to capture cursor blink on phase (32 frames at 50fps = 640ms)
    for (int frame = 0; frame < 32; ++frame) {
        for (int i = 0; i < 80000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }

    // Read CRTC cursor position
    uint16_t cursor_pos = machine.memory().crtc.cursor_position();
    uint16_t screen_start = machine.memory().crtc.screen_start();
    int cursor_offset = cursor_pos - screen_start;
    int cursor_line = cursor_offset / 40;
    int cursor_col = cursor_offset % 40;

    WARN("Initial cursor at line " << cursor_line << ", col " << cursor_col);

    // Save frame
    auto pixels = fb.read_frame();
    save_frame_ppm("/tmp/repro_initial.ppm", pixels, 640, 512);
    WARN("Saved /tmp/repro_initial.ppm");

    // Press Enter by injecting CR via keyboard
    // The MOS reads keyboard via OSRDCH/OSBYTE, which polls the keyboard
    // For simplicity, directly set the keyboard buffer location

    auto inject_enter_and_wait = [&machine, &renderer](int sequence) {
        // The MOS stores the last key in &E7, and the keyboard IRQ fills it
        // Actually we need to trigger keyboard processing by injecting via VIA

        // Simpler: call OSWRCH with CR (13) to output newline
        // But OSWRCH is for output, not input...

        // Let's use the INSV vector to insert a character into the buffer
        // Or just directly manipulate the keyboard row/column state

        // For reproducibility, inject via BBC's keyboard buffer at &03E0-&03FF
        // The buffer pointer is at &0282 (next char) and &0281 (last char)

        // Actually, let's just use the keyboard matrix
        // Return key is row 4, col 9 on the BBC
        // Set keyboard column at VIA port A, read row from VIA port A

        // Even simpler: inject directly into INKEY buffer
        // MOS stores last key at &00E7

        // For this test, let's just write CR to the MOS input buffer
        // The keyboard buffer is at &03E0 with pointers at &0282/&0281

        uint8_t buf_start = machine.read(0x0281);  // Buffer start offset
        uint8_t buf_end = machine.read(0x0282);    // Buffer end offset
        uint8_t buf_size = (buf_end >= buf_start) ? (buf_end - buf_start) : (32 - buf_start + buf_end);

        // Write CR to next position in buffer
        uint8_t next_pos = (buf_end + 1) & 0x1F;
        machine.write(0x03E0 + buf_end, 13);  // CR
        machine.write(0x0282, next_pos);       // Update buffer end pointer

        WARN("Injected Enter (seq " << sequence << "), buffer was " << (int)buf_size << " chars");

        // Run until input is processed (let MOS handle it)
        for (int frame = 0; frame < 64; ++frame) {
            for (int i = 0; i < 80000; ++i) {
                machine.step();
                if (machine.memory().video_output.has_value()) {
                    renderer.process(machine.memory().video_output.value());
                }
            }
        }
    };

    // Press Enter 20 times to go through all lines
    for (int seq = 1; seq <= 20; ++seq) {
        inject_enter_and_wait(seq);

        cursor_pos = machine.memory().crtc.cursor_position();
        screen_start = machine.memory().crtc.screen_start();
        cursor_offset = cursor_pos - screen_start;
        cursor_line = cursor_offset / 40;
        cursor_col = cursor_offset % 40;

        auto px = fb.read_frame();
        std::string filename = "/tmp/repro_enter_" + std::to_string(seq) + ".ppm";
        save_frame_ppm(filename, px, 640, 512);
        WARN("Enter " << seq << ": cursor at line " << cursor_line << ", col " << cursor_col
             << " -> " << filename);
    }
}

TEST_CASE("Save cursor frames for visual inspection", "[video][cursor][visual]") {
    // Render framebuffer images with cursor at different lines
    // Save as PPM files for visual inspection

    REQUIRE(roms_available());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    ModelB machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Boot with rendering
    for (uint64_t i = 0; i < 3'000'000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        if (machine.read(0x7C28) == 'B' && machine.read(0x7C29) == 'B') break;
    }

    // Stabilize
    for (int i = 0; i < 500000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    uint16_t screen_start = machine.memory().crtc.screen_start();
    WARN("Screen start: 0x" << std::hex << screen_start);

    // Force steady cursor (no blink)
    machine.memory().crtc.write(0, 10);
    uint8_t r10 = machine.memory().crtc.read(1);
    machine.memory().crtc.write(0, 10);
    machine.memory().crtc.write(1, r10 & 0x1F);

    // Save frame for each line
    for (int line = 0; line < 25; ++line) {
        // Set cursor to column 1 on this line
        uint16_t cursor_addr = (screen_start + line * 40 + 1) & 0x3FFF;
        machine.memory().crtc.write(0, 14);
        machine.memory().crtc.write(1, (cursor_addr >> 8) & 0x3F);
        machine.memory().crtc.write(0, 15);
        machine.memory().crtc.write(1, cursor_addr & 0xFF);

        // Render one complete frame
        fb.clear(0);
        renderer.reset();
        if (machine.memory().video_output.has_value()) {
            machine.memory().video_output.value().consume(
                machine.memory().video_output.value().size());
        }

        for (int i = 0; i < 80000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
        fb.swap(fb.metadata());

        // Save frame
        auto pixels = fb.read_frame();
        std::string filename = "/tmp/cursor_line_" + std::to_string(line) + ".ppm";
        save_frame_ppm(filename, pixels, 640, 512);
        WARN("Saved " << filename);
    }

    // Also save baseline (no cursor)
    machine.memory().crtc.write(0, 10);
    machine.memory().crtc.write(1, (r10 & 0x1F) | 0x20);  // No cursor

    fb.clear(0);
    renderer.reset();
    if (machine.memory().video_output.has_value()) {
        machine.memory().video_output.value().consume(
            machine.memory().video_output.value().size());
    }

    for (int i = 0; i < 80000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }
    fb.swap(fb.metadata());

    auto pixels = fb.read_frame();
    save_frame_ppm("/tmp/cursor_none.ppm", pixels, 640, 512);
    WARN("Saved /tmp/cursor_none.ppm (no cursor baseline)");
}

TEST_CASE("SAA5050 cursor XOR output", "[video][cursor][saa5050]") {
    // Direct test: verify SAA5050 applies cursor XOR at various column positions
    // This bypasses the framebuffer and tests SAA5050 in isolation

    using namespace beebium;

    Saa5050 saa;

    // Test cursor at different columns
    // SAA5050 has 4-character pipeline delay, so we need to process enough
    // characters to see cursor output after the delay
    for (int cursor_col = 0; cursor_col < 8; ++cursor_col) {
        saa.reset();
        saa.start_of_line();
        saa.set_raster(10);  // Middle of character row

        // Process characters up to and including cursor position + pipeline delay
        // Pipeline delay is 4 characters
        int num_chars = cursor_col + 8;  // Enough to flush pipeline

        // Track which batches have cursor-XORed pixels
        std::vector<bool> batch_has_cursor;

        for (int col = 0; col < num_chars; ++col) {
            bool cursor_here = (col == cursor_col);
            saa.byte(0x20, 1, cursor_here);  // Space character

            // Emit the two batches for this character
            // (But note: due to pipeline, output is delayed)
            PixelBatch pb1, pb2;
            saa.emit_pixels(pb1, bbc_colors::PALETTE);
            saa.emit_pixels(pb2, bbc_colors::PALETTE);

            // Check if batches have cursor XOR
            // With black bg (value=0), cursor XOR gives 0x0FFF (max brightness)
            // Space on black should be all 0s, cursor XOR makes it 0x0FFF
            bool b1_cursor = false, b2_cursor = false;
            for (int p = 0; p < 8; ++p) {
                // Check for specifically XORed pixels (0x0FFF = inverted black)
                if (pb1.pixels.pixels[p].value == 0x0FFF) b1_cursor = true;
                if (pb2.pixels.pixels[p].value == 0x0FFF) b2_cursor = true;
            }

            // Log first batch pixel values for debugging
            if (col == 0) {
                std::string vals;
                for (int p = 0; p < 8; ++p) {
                    vals += std::to_string(pb1.pixels.pixels[p].value) + " ";
                }
                WARN("  col " << col << " batch1 pixels: " << vals);
            }

            batch_has_cursor.push_back(b1_cursor);
            batch_has_cursor.push_back(b2_cursor);
        }

        // Count total cursor batches
        int cursor_batches = 0;
        std::string batch_map;
        for (size_t i = 0; i < batch_has_cursor.size(); ++i) {
            batch_map += batch_has_cursor[i] ? "X" : ".";
            if (batch_has_cursor[i]) ++cursor_batches;
        }

        WARN("Cursor at column " << cursor_col << ": " << cursor_batches
             << " batches with cursor (" << batch_map << ")");

        // Cursor should appear in exactly 2 batches (left and right halves)
        CHECK(cursor_batches == 2);
    }
}

TEST_CASE("Cursor pixels in rendered frame per line", "[video][cursor][pixels]") {
    // Test: Verify cursor produces visible pixels at each line position
    // This checks actual rendered output, not just CRTC detection

    REQUIRE(roms_available());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    ModelB machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Boot to prompt
    for (uint64_t i = 0; i < 3'000'000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        if (machine.read(0x7C28) == 'B' && machine.read(0x7C29) == 'B') {
            break;
        }
    }

    // Stabilize
    for (int i = 0; i < 200000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    // Get screen start
    uint16_t screen_start = machine.memory().crtc.screen_start();
    WARN("Screen start: 0x" << std::hex << screen_start);

    // Force steady cursor (no blink) for consistent measurement
    machine.memory().crtc.write(0, 10);  // Select R10
    uint8_t r10 = machine.memory().crtc.read(1);
    machine.memory().crtc.write(0, 10);
    machine.memory().crtc.write(1, r10 & 0x1F);  // Clear blink mode bits (steady cursor)

    // Count total bright pixels in entire frame
    auto count_total_brightness = [&fb]() -> size_t {
        auto frame = fb.read_frame();
        size_t bright = 0;
        for (auto pixel : frame) {
            uint8_t r = (pixel >> 16) & 0xFF;
            uint8_t g = (pixel >> 8) & 0xFF;
            uint8_t b = pixel & 0xFF;
            if ((r + g + b) / 3 > 128) ++bright;
        }
        return bright;
    };

    // First, get baseline brightness with cursor disabled
    machine.memory().crtc.write(0, 10);
    machine.memory().crtc.write(1, (r10 & 0x1F) | 0x20);  // No cursor

    // Wait for a complete frame using version counter
    uint64_t start_version = fb.version();
    while (fb.version() == start_version) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }
    size_t baseline = count_total_brightness();
    WARN("Baseline brightness (no cursor): " << baseline);

    // Now test each cursor position
    // Restore steady cursor
    machine.memory().crtc.write(0, 10);
    machine.memory().crtc.write(1, r10 & 0x1F);

    struct LineResult {
        int line;
        size_t brightness;
        int64_t diff;
    };
    std::vector<LineResult> results;

    for (int line = 0; line < 25; ++line) {
        // Set cursor to column 1 on this line
        uint16_t cursor_addr = (screen_start + line * 40 + 1) & 0x3FFF;
        machine.memory().crtc.write(0, 14);
        machine.memory().crtc.write(1, (cursor_addr >> 8) & 0x3F);
        machine.memory().crtc.write(0, 15);
        machine.memory().crtc.write(1, cursor_addr & 0xFF);

        // Wait for a complete frame with cursor at new position
        uint64_t frame_version = fb.version();
        while (fb.version() == frame_version) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }

        // Verify cursor position wasn't overwritten by MOS
        machine.memory().crtc.write(0, 14);
        uint8_t read_hi = machine.memory().crtc.read(1);
        machine.memory().crtc.write(0, 15);
        uint8_t read_lo = machine.memory().crtc.read(1);
        uint16_t read_cursor = (static_cast<uint16_t>(read_hi) << 8) | read_lo;

        size_t brightness = count_total_brightness();
        int64_t diff = static_cast<int64_t>(brightness) - static_cast<int64_t>(baseline);

        bool cursor_overwritten = (read_cursor != cursor_addr);
        if (cursor_overwritten) {
            WARN("  Line " << line << ": CURSOR OVERWRITTEN! Set 0x" << std::hex
                 << cursor_addr << " but read 0x" << read_cursor << std::dec);
        }

        results.push_back({line, brightness, diff});
    }

    // Report results
    WARN("Cursor pixel visibility per line (baseline=" << baseline << "):");
    bool all_visible = true;
    for (const auto& r : results) {
        bool cursor_visible = r.diff > 10;  // Cursor should add brightness
        if (!cursor_visible) all_visible = false;

        WARN("  Line " << r.line << ": brightness=" << r.brightness
             << " diff=" << r.diff
             << (cursor_visible ? " [VISIBLE]" : " [MISSING]"));
    }

    CHECK(all_visible);
}

TEST_CASE("Cursor after CRTC register writes", "[video][cursor][diagnostic]") {
    // Test: Simulate moving cursor by writing to CRTC cursor registers
    // This tests cursor detection at various positions without needing keyboard input

    REQUIRE(roms_available());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    ModelB machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.reset();

    // Boot to prompt
    for (uint64_t i = 0; i < 3'000'000; ++i) {
        machine.step();
        if (machine.read(0x7C28) == 'B' && machine.read(0x7C29) == 'B') {
            break;
        }
    }

    // Run until Mode 7 is stable
    for (int i = 0; i < 100000; ++i) {
        machine.step();
    }

    // Get screen start
    machine.memory().crtc.write(0, 12);
    uint8_t start_hi = machine.memory().crtc.read(1);
    machine.memory().crtc.write(0, 13);
    uint8_t start_lo = machine.memory().crtc.read(1);
    uint16_t screen_start = (static_cast<uint16_t>(start_hi) << 8) | start_lo;

    WARN("Screen start: 0x" << std::hex << screen_start);

    // First, discover what row values the CRTC actually produces
    std::set<uint8_t> rows_seen;
    machine.memory().crtc.set_cursor_debug_callback(
        [&rows_seen](uint16_t, uint16_t, uint16_t, uint8_t row, uint8_t, bool) {
            rows_seen.insert(row);
        }
    );

    // Run two frames to ensure complete coverage (Mode 7 CRTC at 1MHz)
    for (int i = 0; i < 100000; ++i) {
        machine.step();
    }
    machine.memory().crtc.clear_cursor_debug_callback();

    WARN("Rows seen in one frame: ");
    std::string row_list;
    for (auto row : rows_seen) {
        row_list += std::to_string(row) + " ";
    }
    WARN("  " << row_list);
    WARN("  Total unique rows: " << rows_seen.size());

    // Test cursor detection at different line positions
    // Mode 7: 40 columns x 25 rows, screen at 0x2800-0x2BFF (1KB circular)
    struct TestPosition {
        int line;           // 0-24 (screen lines)
        uint16_t offset;    // Offset from screen start
        uint64_t matches;
    };

    std::vector<TestPosition> test_positions;
    for (int line = 0; line < 10; ++line) {
        uint16_t offset = line * 40 + 1;  // Column 1 on each line
        test_positions.push_back({line, offset, 0});
    }

    // Test each position
    for (auto& pos : test_positions) {
        // Set cursor position
        uint16_t cursor_addr = (screen_start + pos.offset) & 0x3FFF;
        machine.memory().crtc.write(0, 14);  // Select R14
        machine.memory().crtc.write(1, (cursor_addr >> 8) & 0x3F);
        machine.memory().crtc.write(0, 15);  // Select R15
        machine.memory().crtc.write(1, cursor_addr & 0xFF);

        // Track cursor matches and debug info
        uint64_t local_matches = 0;
        uint8_t target_row = static_cast<uint8_t>(pos.line);
        uint16_t min_char_addr_on_row = 0xFFFF;
        uint16_t max_char_addr_on_row = 0;
        uint64_t samples_on_row = 0;

        machine.memory().crtc.set_cursor_debug_callback(
            [&local_matches, &min_char_addr_on_row, &max_char_addr_on_row, &samples_on_row,
             target_row]
            (uint16_t char_addr, uint16_t /*cursor_pos*/, uint16_t, uint8_t row, uint8_t, bool detected) {
                if (detected) {
                    ++local_matches;
                }
                // Track char_addr range on target row
                if (row == target_row) {
                    ++samples_on_row;
                    if (char_addr < min_char_addr_on_row) min_char_addr_on_row = char_addr;
                    if (char_addr > max_char_addr_on_row) max_char_addr_on_row = char_addr;
                }
            }
        );

        // Run 48 frames (1/16 blink rate means 16 on, 16 off, so 48 ensures at least one on period)
        constexpr int NUM_FRAMES = 48;
        constexpr int CYCLES_PER_FRAME = 80000;
        for (int frame = 0; frame < NUM_FRAMES; ++frame) {
            for (int i = 0; i < CYCLES_PER_FRAME; ++i) {
                machine.step();
            }
        }

        // Read back cursor position to verify it wasn't overwritten
        machine.memory().crtc.write(0, 14);
        uint8_t read_hi = machine.memory().crtc.read(1);
        machine.memory().crtc.write(0, 15);
        uint8_t read_lo = machine.memory().crtc.read(1);
        uint16_t read_cursor = (static_cast<uint16_t>(read_hi) << 8) | read_lo;

        if (read_cursor != cursor_addr) {
            WARN("  Line " << pos.line << ": cursor was overwritten! Set 0x"
                 << std::hex << cursor_addr << " but read back 0x" << read_cursor << std::dec);
        }

        // Log char_addr range on failing lines
        if (local_matches == 0) {
            WARN("  Line " << pos.line << ": row=" << static_cast<int>(target_row)
                 << ", char_addr range 0x" << std::hex
                 << min_char_addr_on_row << "-0x" << max_char_addr_on_row
                 << ", cursor=0x" << cursor_addr
                 << ", samples=" << std::dec << samples_on_row);
        }

        pos.matches = local_matches;
        machine.memory().crtc.clear_cursor_debug_callback();
    }

    // Report results
    WARN("Cursor detection by line position:");
    bool all_detected = true;
    for (const auto& pos : test_positions) {
        bool detected = pos.matches > 0;
        if (!detected) all_detected = false;
        WARN("  Line " << pos.line << " (offset 0x" << std::hex << pos.offset
             << std::dec << "): " << pos.matches << " matches"
             << (detected ? " [OK]" : " [MISSING!]"));
    }

    CHECK(all_detected);
}

TEST_CASE("CRTC cursor register state", "[video][cursor][registers]") {
    // Verify CRTC cursor registers are correctly configured after boot

    REQUIRE(roms_available());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    ModelB machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.reset();

    // Boot to BASIC prompt
    for (uint64_t i = 0; i < 3'000'000; ++i) {
        machine.step();
        if (machine.read(0x7C28) == 'B' && machine.read(0x7C29) == 'B') break;
    }

    // Run more cycles to let BASIC fully initialize cursor
    for (int i = 0; i < 500000; ++i) {
        machine.step();
    }

    // Use direct CRTC accessors for accurate readings
    uint16_t screen_start = machine.memory().crtc.screen_start();
    uint16_t cursor_addr = machine.memory().crtc.cursor_position();

    // Read cursor start/end registers via accessor
    uint8_t r10 = machine.memory().crtc.reg(10);
    uint8_t r11 = machine.memory().crtc.reg(11);

    uint8_t cursor_start_line = r10 & 0x1F;
    uint8_t cursor_end_line = r11 & 0x1F;
    uint8_t cursor_mode = (r10 >> 5) & 0x03;

    WARN("CRTC Cursor State after boot:");
    WARN("  R10 raw: " << std::dec << (int)r10);
    WARN("    Cursor start scanline: " << (int)cursor_start_line);
    WARN("    Cursor mode: " << (int)cursor_mode << " (0=steady, 1=off, 2=blink 1/16, 3=blink 1/32)");
    WARN("  R11 raw: " << (int)r11);
    WARN("    Cursor end scanline: " << (int)cursor_end_line);
    WARN("  Screen start: 0x" << std::hex << screen_start << std::dec << " (" << screen_start << ")");
    WARN("  Cursor address: 0x" << std::hex << cursor_addr << std::dec << " (" << cursor_addr << ")");
    WARN("  Cursor offset from screen: " << (cursor_addr - screen_start));

    // Verify cursor is configured for visible blinking
    CHECK(cursor_mode != 1);  // Not off
    CHECK(cursor_end_line >= cursor_start_line);  // End after start
    CHECK(cursor_addr >= screen_start);  // Cursor on screen

    // Calculate expected cursor line (rows) and column
    int cursor_offset = cursor_addr - screen_start;
    int cursor_line = cursor_offset / 40;
    int cursor_col = cursor_offset % 40;
    WARN("  Cursor at line " << cursor_line << ", column " << cursor_col);
}

TEST_CASE("Cursor blink at each line position", "[video][cursor][perline]") {
    // Test cursor blink at each line position (0-24)
    // Checks if cursor produces periodic pixel variation at every line

    REQUIRE(roms_available());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    ModelB machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Boot to BASIC prompt
    for (uint64_t i = 0; i < 3'000'000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        if (machine.read(0x7C28) == 'B' && machine.read(0x7C29) == 'B') break;
    }

    // Stabilize
    for (int i = 0; i < 200000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    uint16_t screen_start = machine.memory().crtc.screen_start();
    WARN("Screen start: 0x" << std::hex << screen_start << std::dec);

    // Test each line position
    struct LineResult {
        int line;
        size_t min_bright;
        size_t max_bright;
        size_t range;
        bool has_blink;
    };
    std::vector<LineResult> results;

    constexpr int FRAMES_PER_LINE = 64;  // Enough for at least one full blink cycle

    for (int line = 0; line < 25; ++line) {
        // Set cursor to column 1 on this line
        uint16_t cursor_addr = (screen_start + line * 40 + 1) & 0x3FFF;
        machine.memory().crtc.write(0, 14);
        machine.memory().crtc.write(1, (cursor_addr >> 8) & 0x3F);
        machine.memory().crtc.write(0, 15);
        machine.memory().crtc.write(1, cursor_addr & 0xFF);

        // Collect brightness for multiple frames using version-based detection
        std::vector<size_t> brightness;
        for (int frame = 0; frame < FRAMES_PER_LINE; ++frame) {
            uint64_t start_version = fb.version();
            while (fb.version() == start_version) {
                machine.step();
                if (machine.memory().video_output.has_value()) {
                    renderer.process(machine.memory().video_output.value());
                }
            }

            auto pixels = fb.read_frame();
            brightness.push_back(count_bright_pixels(pixels));
        }

        size_t min_val = *std::min_element(brightness.begin(), brightness.end());
        size_t max_val = *std::max_element(brightness.begin(), brightness.end());
        size_t range = max_val - min_val;

        results.push_back({line, min_val, max_val, range, range >= 10});
    }

    // Report results
    WARN("Cursor blink per line (screen_start=0x" << std::hex << screen_start << std::dec << "):");
    int failures = 0;
    for (const auto& r : results) {
        if (!r.has_blink) ++failures;
        WARN("  Line " << r.line << ": min=" << r.min_bright << " max=" << r.max_bright
             << " range=" << r.range << (r.has_blink ? " [OK]" : " [NO BLINK]"));
    }

    // All lines should show cursor blink
    CHECK(failures == 0);
}

TEST_CASE("Simple cursor blink detection", "[video][cursor][simple]") {
    // Simplest test:
    // 1) Boot
    // 2) Count white versus black pixels in every frame
    // 3) Discard the first 50 frames
    // 4) There should be a periodic pattern over time as the cursor blinks

    REQUIRE(roms_available());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    ModelB machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.reset();

    // Boot without video processing (like the passing test does)
    for (uint64_t i = 0; i < 3'000'000; ++i) {
        machine.step();
        if (machine.read(0x7C28) == 'B' && machine.read(0x7C29) == 'B') {
            break;
        }
    }

    // Run extra stabilization cycles
    for (int i = 0; i < 200000; ++i) {
        machine.step();
    }

    // Drain any pending video output
    if (machine.memory().video_output.has_value()) {
        auto& queue = machine.memory().video_output.value();
        queue.consume(queue.size());
    }

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Run and count pixels per frame using version-based frame detection
    constexpr int TOTAL_FRAMES = 150;  // 50 to discard + 100 to analyze

    std::vector<size_t> bright_counts;
    bright_counts.reserve(TOTAL_FRAMES);

    for (int frame = 0; frame < TOTAL_FRAMES; ++frame) {
        // Wait for next complete frame using version counter
        uint64_t start_version = fb.version();
        while (fb.version() == start_version) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }

        // Count bright pixels in completed frame
        auto pixels = fb.read_frame();
        size_t bright = count_bright_pixels(pixels);
        bright_counts.push_back(bright);
    }

    // Discard first 50 frames (boot sequence)
    std::vector<size_t> stable_counts(bright_counts.begin() + 50, bright_counts.end());

    // Find min/max in stable region
    size_t min_val = *std::min_element(stable_counts.begin(), stable_counts.end());
    size_t max_val = *std::max_element(stable_counts.begin(), stable_counts.end());
    size_t range = max_val - min_val;

    INFO("Stable frames (50-149): min=" << min_val << " max=" << max_val << " range=" << range);

    // Output all stable frame values for analysis
    std::string values;
    for (size_t i = 0; i < stable_counts.size(); ++i) {
        values += std::to_string(stable_counts[i]);
        if (i < stable_counts.size() - 1) values += ", ";
        if ((i + 1) % 16 == 0) values += "\n    ";
    }
    WARN("Stable frame brightness values:\n    " << values);

    // The cursor blinks at 1/16 field rate (16 frames on, 16 off)
    // Look for this periodic pattern by checking transitions
    int transitions = 0;
    constexpr size_t THRESHOLD = 5;  // Minimum change to count as cursor toggle

    for (size_t i = 1; i < stable_counts.size(); ++i) {
        size_t diff = (stable_counts[i] > stable_counts[i-1]) ?
                      (stable_counts[i] - stable_counts[i-1]) :
                      (stable_counts[i-1] - stable_counts[i]);
        if (diff >= THRESHOLD) {
            ++transitions;
        }
    }

    INFO("Transitions detected: " << transitions);

    // With 100 frames at 1/16 blink rate, expect ~6 transitions (100/16 ≈ 6)
    // If cursor is working, we should see periodic variation
    CHECK(range >= 10);  // Should have measurable variation
    CHECK(transitions >= 2);  // Should have at least a couple transitions
}

#endif // BEEBIUM_ROM_DIR
