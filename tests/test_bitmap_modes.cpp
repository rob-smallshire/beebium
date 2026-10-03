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

// test_bitmap_modes.cpp
//
// Diagnostic tests for bitmap MODE 0-6 rendering.
// Also includes golden master tests for MODE 1 and MODE 2.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <beebium/Machines.hpp>
#include <beebium/PixelBatch.hpp>
#include <beebium/FrameAllocator.hpp>
#include <beebium/FrameBuffer.hpp>
#include <beebium/FrameRenderer.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <cmath>

#include "test_keyboard_helpers.hpp"

using namespace beebium;
using namespace beebium::test;

// ============================================================================
// Machine Type Configuration Traits
// ============================================================================

// ROM configuration per machine type
template<typename MachineType>
struct RomConfig;

template<>
struct RomConfig<ModelB> {
    static constexpr const char* mos_filename = "acorn-mos_1_20.rom";
};

template<>
struct RomConfig<ModelBPlus> {
    static constexpr const char* mos_filename = "acorn-mos_2_0.rom";
};

// Golden master directory per machine type
template<typename MachineType>
struct GoldenConfig;

template<>
struct GoldenConfig<ModelB> {
    static constexpr const char* subdir = "model-b";
};

template<>
struct GoldenConfig<ModelBPlus> {
    static constexpr const char* subdir = "model-b-plus";
};

namespace {

// ============================================================================
// PPM File Utilities
// ============================================================================

// Write BGRA32 framebuffer to PPM file (P6 binary format)
// stride_pixels is the number of pixels per row in the buffer (may be > width for padding)
bool write_ppm(const std::filesystem::path& filepath,
               const uint32_t* pixels,
               size_t width, size_t height, size_t stride_pixels = 0) {
    std::ofstream file(filepath, std::ios::binary);
    if (!file) return false;

    // If stride not specified, assume tightly packed
    if (stride_pixels == 0) stride_pixels = width;

    file << "P6\n" << width << " " << height << "\n255\n";

    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            uint32_t pixel = pixels[y * stride_pixels + x];
            uint8_t b = (pixel >> 0) & 0xFF;
            uint8_t g = (pixel >> 8) & 0xFF;
            uint8_t r = (pixel >> 16) & 0xFF;
            file.put(static_cast<char>(r));
            file.put(static_cast<char>(g));
            file.put(static_cast<char>(b));
        }
    }

    return file.good();
}

// Read PPM file into BGRA32 buffer
bool read_ppm(const std::filesystem::path& filepath,
              std::vector<uint32_t>& pixels,
              size_t& width, size_t& height) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) return false;

    std::string magic;
    file >> magic;
    if (magic != "P6") return false;

    // Skip comments
    char c;
    file.get(c);
    while (file.peek() == '#') {
        std::string comment;
        std::getline(file, comment);
    }

    size_t w, h, maxval;
    file >> w >> h >> maxval;
    if (maxval != 255) return false;

    file.get(c);  // Skip single whitespace after maxval

    width = w;
    height = h;
    pixels.resize(w * h);

    for (size_t i = 0; i < w * h; ++i) {
        uint8_t r, g, b;
        file.read(reinterpret_cast<char*>(&r), 1);
        file.read(reinterpret_cast<char*>(&g), 1);
        file.read(reinterpret_cast<char*>(&b), 1);
        pixels[i] = (0xFF << 24) | (r << 16) | (g << 8) | b;
    }

    return file.good() || file.eof();
}

// Exact pixel comparison for golden master tests.
// a_stride is pixels per row in buffer a (may include padding)
// b is assumed to be tightly packed (width pixels per row)
size_t compare_frames_exact(const uint32_t* a, size_t a_stride,
                            const uint32_t* b,
                            size_t width, size_t height) {
    size_t diff = 0;
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            uint32_t pa = a[y * a_stride + x];
            uint32_t pb = b[y * width + x];

            // Compare RGB, ignore alpha
            if ((pa & 0x00FFFFFF) != (pb & 0x00FFFFFF)) {
                ++diff;
            }
        }
    }
    return diff;
}

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

// Check if ROMs are available for a specific machine type
template<typename MachineType>
bool roms_available() {
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    return std::filesystem::exists(rom_dirpath / RomConfig<MachineType>::mos_filename) &&
           std::filesystem::exists(rom_dirpath / "bbc-basic_2.rom");
}

// Non-templated version for diagnostic tests (ModelB only)
bool roms_available() {
    return roms_available<ModelB>();
}

// Base path for golden master files
const std::filesystem::path GOLDEN_BASE = std::filesystem::path(BEEBIUM_ROM_DIR).parent_path() / "tests" / "golden";

// Get golden directory for a specific machine type and mode
template<typename MachineType>
std::filesystem::path golden_dir(const std::string& mode) {
    return GOLDEN_BASE / GoldenConfig<MachineType>::subdir / mode;
}

// Generate and type the printable chars testcard BASIC program
// max_col = cols - 1 (e.g., 79 for 80-column mode)
// max_row = rows - 1 (e.g., 31 for 32-row mode)
// cycles_per_key: 100000 is the minimum reliable typing speed
template<typename MachineType>
void type_printable_chars_testcard(MachineType& machine, FrameRenderer& renderer,
                                    int max_col, int max_row, int cycles_per_key = 100000) {
    // Build the BASIC program string
    // The inner loop uses {C}+(Y={R}) to print one less character on the last row,
    // avoiding the bottom-right cell which would cause a scroll when printed.
    // (In BBC BASIC, TRUE=-1, so when Y={R}, the limit becomes {C}-1)
    // The REPEAT:UNTIL0 at the end creates an infinite loop to prevent the BASIC prompt from causing a scroll.
    std::string program = "10CLS:FOR I=0TO" + std::to_string(max_col) +
                          ":P.CHR$(48+I MOD10);:N.:FOR Y=1TO" + std::to_string(max_row) +
                          ":P.CHR$(48+Y MOD10);:FOR X=1TO" + std::to_string(max_col) +
                          "+(Y=" + std::to_string(max_row) + ")" +
                          ":P.CHR$(32+((Y-1)*" + std::to_string(max_col) + "+X-1)MOD95);:N.:N.:REPEAT:UNTIL0";

    // Type the program
    type_string_with_shift(machine, renderer, program.c_str(), cycles_per_key);

    // Press RETURN to enter the program
    type_string_with_shift(machine, renderer, "\r", cycles_per_key);

    // Small delay for program entry
    for (int i = 0; i < 100000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    // Type RUN and RETURN
    type_string_with_shift(machine, renderer, "RUN\r", cycles_per_key);

    // Wait for program execution (scales with screen size)
    // Each character print in BASIC involves loop overhead, CHR$ call, and PRINT.
    // At 2MHz, this takes roughly 20000 cycles per character position.
    int execution_cycles = (max_col + 1) * (max_row + 1) * 20000;
    for (int i = 0; i < execution_cycles; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }
}

// Force cursor to steady (non-blinking) mode for deterministic golden masters.
// CRTC R10 bits 5-6 control cursor mode: 00=steady, 01=none, 10=blink/16, 11=blink/32
// We preserve the MOS's cursor start line and only change the mode to steady.
template<typename MachineType>
void force_steady_cursor(MachineType& machine) {
    auto& crtc = machine.memory().crtc;
    // R10: Cursor Start - bits 0-4 = start line, bits 5-6 = mode
    // Read current value, clear mode bits, set mode to steady (00)
    uint8_t r10 = crtc.reg(10);
    uint8_t cursor_start_line = r10 & 0x1F;  // Preserve bits 0-4
    // Write back with mode bits = 00 (steady)
    crtc.write(0, 10);  // Select R10
    crtc.write(1, cursor_start_line);  // Start line preserved, mode = steady
}

// Set up AUG MODE 8 - a custom 80x256 pixel, 16-color mode from the
// Advanced User Guide for the BBC Microcomputer.
// Must be called after booting into MODE 5.
// Text grid: 10 columns x 32 rows
template<typename MachineType>
void setup_aug_mode8(MachineType& machine, FrameRenderer& renderer) {
    const char* program = R"(MODE 5
*FX 154,224
?&360=&F
?&361=1
?&34F=&20
?&363=&55
?&362=&AA
?&30A=9
VDU 20)";
    type_basic_program(machine, renderer, program);
    // Wait for mode switch to complete
    for (int i = 0; i < 500000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }
}

#endif

} // anonymous namespace

#ifdef BEEBIUM_ROM_DIR

TEST_CASE("Diagnostic: VideoUla emit_pixels produces correct colors", "[bitmap][diagnostic]") {
    VideoUla ula;
    ula.reset();

    // Configure for Mode 0: fast_clock=1, line_width=3, teletext=0
    // Control = 0b0001_1100 = 0x1C
    ula.write(0, 0x1C);

    SECTION("emit_pixels with 0xFF byte and programmed palette") {
        // Program palette: all logical indices -> physical 7 (white)
        // Write value: (index << 4) | (physical ^ 7)
        // For physical 7: write (7 ^ 7) = 0
        for (int i = 0; i < 16; ++i) {
            ula.write(1, (i << 4) | 0);  // All indices -> white
        }

        // Feed a byte with all bits set
        ula.byte(0xFF, false);

        PixelBatch batch;
        ula.emit_pixels(batch);

        // All 8 pixels should be white
        INFO("Batch type: " << (int)batch.type());
        for (int i = 0; i < 8; ++i) {
            INFO("Pixel[" << i << "] = 0x" << std::hex << batch.pixels.pixels[i].value);
            CHECK(batch.pixels.pixels[i].value != 0);
        }
    }

    SECTION("emit_pixels with 0x00 byte produces black") {
        // Program palette: all logical indices -> physical 0 (black)
        // For physical 0: write (0 ^ 7) = 7
        for (int i = 0; i < 16; ++i) {
            ula.write(1, (i << 4) | 7);  // All indices -> black
        }

        ula.byte(0x00, false);

        PixelBatch batch;
        ula.emit_pixels(batch);

        // Check color values (skip pixels[1] and pixels[2] which store flags/metadata)
        for (int i = 0; i < 8; ++i) {
            if (i == 1 || i == 2) continue;  // Skip metadata slots
            INFO("Pixel[" << i << "] = 0x" << std::hex << batch.pixels.pixels[i].value);
            CHECK(batch.pixels.pixels[i].value == 0);
        }
    }

    SECTION("emit_pixels with alternating bits and 2-color palette") {
        // Program palette for 2-color mode (like Mode 0)
        // Based on bit 7 of the 4-bit index (extracted from bits 7,5,3,1):
        // - Index 0-7 (bit 3 = 0): black
        // - Index 8-15 (bit 3 = 1): white
        for (int i = 0; i < 8; ++i) {
            ula.write(1, (i << 4) | 7);  // Indices 0-7 -> black
        }
        for (int i = 8; i < 16; ++i) {
            ula.write(1, (i << 4) | 0);  // Indices 8-15 -> white
        }

        // Test with 0xAA = 0b10101010
        // Bit pattern for extraction (bits 7,5,3,1): 1,0,1,0 = 10 (0xA) for first pixel
        // After shift: bits 7,5,3,1 of shifted byte give second pixel
        ula.byte(0xAA, false);

        PixelBatch batch;
        ula.emit_pixels(batch);

        INFO("0xAA byte pixel output:");
        for (int i = 0; i < 8; ++i) {
            INFO("  Pixel[" << i << "] = 0x" << std::hex << batch.pixels.pixels[i].value);
        }

        // First pixel should be white (index >= 8)
        // In Mode 0, the actual display depends on how many times shift is called
        // Let's just verify we get some variation
        size_t white_count = 0;
        for (int i = 0; i < 8; ++i) {
            if (batch.pixels.pixels[i].value != 0) {
                ++white_count;
            }
        }
        INFO("White pixel count: " << white_count);
        CHECK(white_count > 0);
        CHECK(white_count < 8);  // Should have some of each
    }
}

TEST_CASE("Diagnostic: Mode 0 ULA configuration", "[bitmap][diagnostic]") {
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

    // Boot to BASIC
    for (uint64_t i = 0; i < 3'000'000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        if (machine.read(0x7C28) == 'B') break;
    }

    // Wait for BASIC prompt
    for (int i = 0; i < 200000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    INFO("Initial mode - should be Mode 7 (teletext)");
    CHECK(machine.memory().video_ula.teletext_mode());
    INFO("Control register: " << std::hex << (int)machine.memory().video_ula.control());

    // Now let's check what Mode 0 ULA settings should be
    // Mode 0: fast_clock=1, line_width=3, teletext=0

    // Simulate MODE 0 by writing to control register
    // Control byte for Mode 0: 0x9C = fast_clock(4) | line_width(3<<2) | teletext(0)
    // Actually: bits 2-3 = line_width, bit 4 = fast_clock
    // Mode 0: line_width=3, fast_clock=1 -> 0b0001_1100 = 0x1C
    // Plus cursor width... let's check what the MOS sets

    SECTION("Check Mode 7 palette state") {
        // Print current palette for debugging
        INFO("Mode 7 palette entries:");
        for (int i = 0; i < 16; ++i) {
            VideoDataPixel p = machine.memory().video_ula.output_palette(i);
            INFO("  Palette[" << i << "] = R:" << (int)p.bits.r
                 << " G:" << (int)p.bits.g << " B:" << (int)p.bits.b);
        }
    }
}

TEST_CASE("Diagnostic: Mode 0 manual setup", "[bitmap][diagnostic]") {
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

    // Boot to BASIC first to get stable state
    for (uint64_t i = 0; i < 3'000'000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        if (machine.read(0x7C28) == 'B') break;
    }

    for (int i = 0; i < 200000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    INFO("Initial Mode 7 state:");
    INFO("Teletext mode: " << machine.memory().video_ula.teletext_mode());
    INFO("Control register: 0x" << std::hex << (int)machine.memory().video_ula.control());

    // Print Mode 7 palette for reference
    INFO("Mode 7 palette entries (before switch):");
    for (int i = 0; i < 16; ++i) {
        VideoDataPixel p = machine.memory().video_ula.output_palette(i);
        INFO("  Palette[" << i << "] = R:" << (int)p.bits.r
             << " G:" << (int)p.bits.g << " B:" << (int)p.bits.b);
    }

    CHECK(machine.memory().video_ula.teletext_mode());
}

TEST_CASE("Diagnostic: Direct screen memory write in Mode 0", "[bitmap][diagnostic]") {
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

    // Boot to BASIC
    for (uint64_t i = 0; i < 3'000'000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        if (machine.read(0x7C28) == 'B') break;
    }

    for (int i = 0; i < 200000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    // Manually configure Mode 0 without using the MOS
    // This lets us test the video hardware directly

    // 1. Set VideoULA control for Mode 0
    // Mode 0: teletext=0, line_width=3, fast_clock=1, cursor=1
    // Control = 0b0001_1100 = 0x1C (plus cursor bits in 5-7)
    machine.memory().video_ula.write(0, 0x9C);  // With cursor width

    // 2. Set palette for 2-color mode
    // In 2-color modes, typically:
    // - Logical colors 0,2,4,6,8,10,12,14 (even) -> physical 0 (black)
    // - Logical colors 1,3,5,7,9,11,13,15 (odd) -> physical 7 (white)
    // The ULA XORs the written value with 7 to get physical color.
    // To get physical 0: write (0 ^ 7) = 7
    // To get physical 7: write (7 ^ 7) = 0
    for (int i = 0; i < 16; ++i) {
        uint8_t write_val = (i << 4) | ((i & 1) ? 7 : 0);
        machine.memory().video_ula.write(1, write_val);
    }

    // 3. Set CRTC registers for Mode 0
    // Mode 0: 80 columns x 32 rows, 8 scanlines per character
    // Screen at 0x3000, so CRTC start address = 0x3000 >> 3 = 0x0600
    auto& crtc = machine.memory().crtc;
    crtc.write(0, 0); crtc.write(1, 127);  // R0: H total = 128 chars
    crtc.write(0, 1); crtc.write(1, 80);   // R1: H displayed = 80 chars
    crtc.write(0, 2); crtc.write(1, 98);   // R2: H sync pos
    crtc.write(0, 3); crtc.write(1, 0x28); // R3: Sync widths
    crtc.write(0, 4); crtc.write(1, 38);   // R4: V total = 39 rows
    crtc.write(0, 5); crtc.write(1, 0);    // R5: V adjust
    crtc.write(0, 6); crtc.write(1, 32);   // R6: V displayed = 32 rows
    crtc.write(0, 7); crtc.write(1, 34);   // R7: V sync pos
    crtc.write(0, 8); crtc.write(1, 0);    // R8: Interlace = off
    crtc.write(0, 9); crtc.write(1, 7);    // R9: Max scanline = 7
    crtc.write(0, 12); crtc.write(1, 0x06); // R12: Start addr high = 0x06 (0x3000 >> 3 = 0x0600)
    crtc.write(0, 13); crtc.write(1, 0x00); // R13: Start addr low = 0x00

    // 4. Set addressable latch for screen base
    // For Mode 0 at 0x3000: screen_base bits = 00
    // Bits 4-5 of IC32 control screen base
    machine.memory().addressable_latch.write(4, false);  // Clear bit 4 (screen base low)
    machine.memory().addressable_latch.write(5, false);  // Clear bit 5 (screen base high)

    INFO("Screen base after setup: " << (int)machine.memory().addressable_latch.screen_base());
    REQUIRE(machine.memory().addressable_latch.screen_base() == 0);

    // 5. Write a test pattern to screen memory
    // Mode 0: 80 chars/line, 8 bytes per char (one byte per scanline)
    // Screen at 0x3000
    // First character is at 0x3000 (8 bytes for 8 scanlines)

    // Write all 0xFF (all white pixels) to the first 16 characters
    // This should give us 16 * 8 * 8 = 1024 white pixels from the pattern
    for (int ch = 0; ch < 16; ++ch) {
        for (int row = 0; row < 8; ++row) {
            uint16_t addr = 0x3000 + ch * 8 + row;
            machine.write(addr, 0xFF);  // All pixels white
        }
    }

    // Also write some black (0x00) to contrast
    for (int ch = 16; ch < 32; ++ch) {
        for (int row = 0; row < 8; ++row) {
            uint16_t addr = 0x3000 + ch * 8 + row;
            machine.write(addr, 0x00);  // All pixels black
        }
    }

    // Verify the write
    INFO("Screen memory after write:");
    INFO("  0x3000 (should be 0xFF): " << std::hex << (int)machine.read(0x3000));
    INFO("  0x3080 (should be 0x00): " << std::hex << (int)machine.read(0x3080));

    // 6. Render a few frames
    fb.clear(0);
    renderer.reset();
    if (machine.memory().video_output.has_value()) {
        machine.memory().video_output.value().consume(
            machine.memory().video_output.value().size());
    }

    for (int frame = 0; frame < 2; ++frame) {
        for (int i = 0; i < 80000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }
    fb.swap(fb.metadata());

    // 7. Check if any non-black pixels were produced
    auto frame_data = fb.read_frame();
    size_t non_black = 0;
    size_t total_white = 0;
    for (size_t i = 0; i < frame_data.size(); ++i) {
        uint32_t pixel = frame_data[i];
        if ((pixel & 0x00FFFFFF) != 0) {
            ++non_black;
            if ((pixel & 0x00FFFFFF) == 0x00FFFFFF) {
                ++total_white;
            }
        }
    }

    INFO("Non-black pixels: " << non_black);
    INFO("White pixels: " << total_white);
    INFO("Total pixels: " << frame_data.size());

    // We expect to see non-black pixels from our pattern
    CHECK(non_black > 0);
}

// ============================================================================
// Golden Master Tests - MODE 0
// ============================================================================

TEMPLATE_TEST_CASE("MODE 0 boot screen golden master", "[mode0][boot][golden]",
                   ModelB, ModelBPlus) {
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();

    // Set startup screen mode to 0 BEFORE reset (uses startup links)
    machine.memory().set_screen_mode(0);
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Boot to BASIC - machine will start in Mode 0
    // Mode 0 is 640x256, 2 colors, 8 pixels per byte
    bool boot_complete = false;
    for (uint64_t i = 0; i < 4'000'000 && !boot_complete; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }

        // Check startUpOptions at $028F - bits 0-2 contain screen mode
        if (i > 2'000'000) {
            uint8_t startup_opts = machine.read(0x028F);
            if ((startup_opts & 0x07) == 0) {
                boot_complete = true;
            }
        }
    }

    // Verify we're in Mode 0 (not teletext)
    REQUIRE_FALSE(machine.memory().video_ula.teletext_mode());

    // Verify startup options shows Mode 0
    uint8_t actual_screen_mode = machine.read(0x028F) & 0x07;
    REQUIRE(actual_screen_mode == 0);

    // Force cursor to steady (non-blinking) mode for deterministic capture
    force_steady_cursor(machine);

    // Run additional frames for stable output
    for (int frame = 0; frame < 4; ++frame) {
        for (int i = 0; i < 80000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }

    auto frame = fb.read_frame();
    auto golden_dirpath = golden_dir<TestType>("mode0");
    std::filesystem::create_directories(golden_dirpath);

    // Check for golden master
    auto golden_filepath = golden_dirpath / "boot_screen.ppm";
    if (std::filesystem::exists(golden_filepath)) {
        std::vector<uint32_t> golden;
        size_t golden_w, golden_h;

        REQUIRE(read_ppm(golden_filepath, golden, golden_w, golden_h));
        REQUIRE(golden_w == fb.width());
        REQUIRE(golden_h == fb.height());

        size_t diff = compare_frames_exact(frame.data(), fb.stride_pixels(),
                                           golden.data(),
                                           fb.width(), fb.height());
        INFO("Pixel difference: " << diff);
        CHECK(diff == 0);
    } else {
        auto output_filepath = golden_dirpath / "boot_screen_candidate.ppm";
        REQUIRE(write_ppm(output_filepath, frame.data(), fb.width(), fb.height(), fb.stride_pixels()));
        WARN("Golden master not found. Candidate saved to: " << output_filepath);
    }
}

// ============================================================================
// Golden Master Tests - MODE 1
// ============================================================================

TEMPLATE_TEST_CASE("MODE 1 boot screen golden master", "[mode1][boot][golden]",
                   ModelB, ModelBPlus) {
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();

    // Set startup screen mode to 1 BEFORE reset (uses startup links)
    machine.memory().set_screen_mode(1);
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Boot to BASIC - machine will start in Mode 1
    // In Mode 1, screen memory is at 0x3000, not 0x7C00
    // The "BBC Computer" message will appear in Mode 1 format
    bool boot_complete = false;
    for (uint64_t i = 0; i < 4'000'000 && !boot_complete; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }

        // Check startUpOptions at $028F - bits 0-2 contain screen mode
        // Once the MOS has finished initializing, this will be set
        if (i > 2'000'000) {
            uint8_t startup_opts = machine.read(0x028F);
            if ((startup_opts & 0x07) == 1) {
                boot_complete = true;
            }
        }
    }

    // Verify we're in Mode 1 (not teletext)
    REQUIRE_FALSE(machine.memory().video_ula.teletext_mode());

    // Verify startup options shows Mode 1
    uint8_t actual_screen_mode = machine.read(0x028F) & 0x07;
    REQUIRE(actual_screen_mode == 1);

    // Force cursor to steady (non-blinking) mode for deterministic capture
    force_steady_cursor(machine);

    // Run additional frames for stable output
    for (int frame = 0; frame < 4; ++frame) {
        for (int i = 0; i < 80000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }

    auto frame = fb.read_frame();
    auto golden_dirpath = golden_dir<TestType>("mode1");
    std::filesystem::create_directories(golden_dirpath);

    // Check for golden master
    auto golden_filepath = golden_dirpath / "boot_screen.ppm";
    if (std::filesystem::exists(golden_filepath)) {
        std::vector<uint32_t> golden;
        size_t golden_w, golden_h;

        REQUIRE(read_ppm(golden_filepath, golden, golden_w, golden_h));
        REQUIRE(golden_w == fb.width());
        REQUIRE(golden_h == fb.height());

        size_t diff = compare_frames_exact(frame.data(), fb.stride_pixels(),
                                           golden.data(),
                                           fb.width(), fb.height());
        INFO("Pixel difference: " << diff);
        CHECK(diff == 0);
    } else {
        auto output_filepath = golden_dirpath / "boot_screen_candidate.ppm";
        REQUIRE(write_ppm(output_filepath, frame.data(), fb.width(), fb.height(), fb.stride_pixels()));
        WARN("Golden master not found. Candidate saved to: " << output_filepath);
    }
}

// ============================================================================
// Golden Master Tests - MODE 2
// ============================================================================

TEMPLATE_TEST_CASE("MODE 2 boot screen golden master", "[mode2][boot][golden]",
                   ModelB, ModelBPlus) {
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();

    // Set startup screen mode to 2 BEFORE reset (uses startup links)
    machine.memory().set_screen_mode(2);
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Boot to BASIC - machine will start in Mode 2
    // In Mode 2, screen memory is at 0x3000-0x7FFF (20KB)
    // The "BBC Computer" message will appear in Mode 2 format
    bool boot_complete = false;
    for (uint64_t i = 0; i < 4'000'000 && !boot_complete; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }

        // Check startUpOptions at $028F - bits 0-2 contain screen mode
        // Once the MOS has finished initializing, this will be set
        if (i > 2'000'000) {
            uint8_t startup_opts = machine.read(0x028F);
            if ((startup_opts & 0x07) == 2) {
                boot_complete = true;
            }
        }
    }

    // Verify we're in Mode 2 (not teletext)
    REQUIRE_FALSE(machine.memory().video_ula.teletext_mode());

    // Verify startup options shows Mode 2
    uint8_t actual_screen_mode = machine.read(0x028F) & 0x07;
    REQUIRE(actual_screen_mode == 2);

    // Force cursor to steady (non-blinking) mode for deterministic capture
    force_steady_cursor(machine);

    // Run additional frames for stable output
    for (int frame = 0; frame < 4; ++frame) {
        for (int i = 0; i < 80000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }

    auto frame = fb.read_frame();
    auto golden_dirpath = golden_dir<TestType>("mode2");
    std::filesystem::create_directories(golden_dirpath);

    // Check for golden master
    auto golden_filepath = golden_dirpath / "boot_screen.ppm";
    if (std::filesystem::exists(golden_filepath)) {
        std::vector<uint32_t> golden;
        size_t golden_w, golden_h;

        REQUIRE(read_ppm(golden_filepath, golden, golden_w, golden_h));
        REQUIRE(golden_w == fb.width());
        REQUIRE(golden_h == fb.height());

        size_t diff = compare_frames_exact(frame.data(), fb.stride_pixels(),
                                           golden.data(),
                                           fb.width(), fb.height());
        INFO("Pixel difference: " << diff);
        CHECK(diff == 0);
    } else {
        auto output_filepath = golden_dirpath / "boot_screen_candidate.ppm";
        REQUIRE(write_ppm(output_filepath, frame.data(), fb.width(), fb.height(), fb.stride_pixels()));
        WARN("Golden master not found. Candidate saved to: " << output_filepath);
    }
}

// ============================================================================
// Golden Master Tests - MODE 3
// ============================================================================

TEMPLATE_TEST_CASE("MODE 3 boot screen golden master", "[mode3][boot][golden]",
                   ModelB, ModelBPlus) {
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();

    // Set startup screen mode to 3 BEFORE reset (uses startup links)
    machine.memory().set_screen_mode(3);
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Boot to BASIC - machine will start in Mode 3
    // Mode 3: 80x25 text, 8x10 character cells (8 rows data + 2 gap rows)
    // Screen memory at 0x4000-0x7FFF (16KB)
    bool boot_complete = false;
    for (uint64_t i = 0; i < 4'000'000 && !boot_complete; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }

        // Check startUpOptions at $028F - bits 0-2 contain screen mode
        if (i > 2'000'000) {
            uint8_t startup_opts = machine.read(0x028F);
            if ((startup_opts & 0x07) == 3) {
                boot_complete = true;
            }
        }
    }

    // Verify we're in Mode 3 (not teletext)
    REQUIRE_FALSE(machine.memory().video_ula.teletext_mode());

    // Verify startup options shows Mode 3
    uint8_t actual_screen_mode = machine.read(0x028F) & 0x07;
    REQUIRE(actual_screen_mode == 3);

    // Force cursor to steady (non-blinking) mode for deterministic capture
    force_steady_cursor(machine);

    // Run additional frames for stable output
    for (int frame = 0; frame < 4; ++frame) {
        for (int i = 0; i < 80000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }

    auto frame = fb.read_frame();
    auto golden_dirpath = golden_dir<TestType>("mode3");
    std::filesystem::create_directories(golden_dirpath);

    // Check for golden master
    auto golden_filepath = golden_dirpath / "boot_screen.ppm";
    if (std::filesystem::exists(golden_filepath)) {
        std::vector<uint32_t> golden;
        size_t golden_w, golden_h;

        REQUIRE(read_ppm(golden_filepath, golden, golden_w, golden_h));
        REQUIRE(golden_w == fb.width());
        REQUIRE(golden_h == fb.height());

        size_t diff = compare_frames_exact(frame.data(), fb.stride_pixels(),
                                           golden.data(),
                                           fb.width(), fb.height());
        INFO("Pixel difference: " << diff);
        CHECK(diff == 0);
    } else {
        auto output_filepath = golden_dirpath / "boot_screen_candidate.ppm";
        REQUIRE(write_ppm(output_filepath, frame.data(), fb.width(), fb.height(), fb.stride_pixels()));
        WARN("Golden master not found. Candidate saved to: " << output_filepath);
    }
}

// ============================================================================
// Golden Master Tests - MODE 4
// ============================================================================

TEMPLATE_TEST_CASE("MODE 4 boot screen golden master", "[mode4][boot][golden]",
                   ModelB, ModelBPlus) {
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();

    // Set startup screen mode to 4 BEFORE reset (uses startup links)
    machine.memory().set_screen_mode(4);
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Boot to BASIC - machine will start in Mode 4
    // Mode 4: 320x256, 2 colors (1bpp), same geometry as Mode 1
    // Screen memory at 0x5800-0x7FFF (10KB)
    bool boot_complete = false;
    for (uint64_t i = 0; i < 4'000'000 && !boot_complete; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }

        // Check startUpOptions at $028F - bits 0-2 contain screen mode
        if (i > 2'000'000) {
            uint8_t startup_opts = machine.read(0x028F);
            if ((startup_opts & 0x07) == 4) {
                boot_complete = true;
            }
        }
    }

    // Verify we're in Mode 4 (not teletext)
    REQUIRE_FALSE(machine.memory().video_ula.teletext_mode());

    // Verify startup options shows Mode 4
    uint8_t actual_screen_mode = machine.read(0x028F) & 0x07;
    REQUIRE(actual_screen_mode == 4);

    // Force cursor to steady (non-blinking) mode for deterministic capture
    force_steady_cursor(machine);

    // Run additional frames for stable output
    for (int frame = 0; frame < 4; ++frame) {
        for (int i = 0; i < 80000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }

    auto frame = fb.read_frame();
    auto golden_dirpath = golden_dir<TestType>("mode4");
    std::filesystem::create_directories(golden_dirpath);

    // Check for golden master
    auto golden_filepath = golden_dirpath / "boot_screen.ppm";
    if (std::filesystem::exists(golden_filepath)) {
        std::vector<uint32_t> golden;
        size_t golden_w, golden_h;

        REQUIRE(read_ppm(golden_filepath, golden, golden_w, golden_h));
        REQUIRE(golden_w == fb.width());
        REQUIRE(golden_h == fb.height());

        size_t diff = compare_frames_exact(frame.data(), fb.stride_pixels(),
                                           golden.data(),
                                           fb.width(), fb.height());
        INFO("Pixel difference: " << diff);
        CHECK(diff == 0);
    } else {
        auto output_filepath = golden_dirpath / "boot_screen_candidate.ppm";
        REQUIRE(write_ppm(output_filepath, frame.data(), fb.width(), fb.height(), fb.stride_pixels()));
        WARN("Golden master not found. Candidate saved to: " << output_filepath);
    }
}

// ============================================================================
// MODE 5 Golden Master Test
// ============================================================================

TEMPLATE_TEST_CASE("MODE 5 boot screen golden master", "[mode5][golden][bitmap]",
                   ModelB, ModelBPlus) {
    // MODE 5: 160x256, 4 colors (2bpp), same geometry as MODE 2
    // Uses slow clock (1MHz CRTC) with 4 pixels per byte
    // Screen memory at 0x5800-0x7FFF (10KB)
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();

    // Set startup screen mode to 5 BEFORE reset (uses startup links)
    machine.memory().set_screen_mode(5);
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Boot to BASIC - machine will start in Mode 5
    bool boot_complete = false;
    for (uint64_t i = 0; i < 4'000'000 && !boot_complete; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }

        // Check startUpOptions at $028F - bits 0-2 contain screen mode
        if (i > 2'000'000) {
            uint8_t startup_opts = machine.read(0x028F);
            if ((startup_opts & 0x07) == 5) {
                boot_complete = true;
            }
        }
    }

    // Verify we're in Mode 5 (not teletext)
    REQUIRE_FALSE(machine.memory().video_ula.teletext_mode());

    // Verify startup options shows Mode 5
    uint8_t actual_screen_mode = machine.read(0x028F) & 0x07;
    REQUIRE(actual_screen_mode == 5);

    // Force cursor to steady (non-blinking) mode for deterministic capture
    force_steady_cursor(machine);

    // Run additional frames for stable output
    for (int frame = 0; frame < 4; ++frame) {
        for (int i = 0; i < 80000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }

    auto frame = fb.read_frame();
    auto golden_dirpath = golden_dir<TestType>("mode5");
    std::filesystem::create_directories(golden_dirpath);

    // Check for golden master
    auto golden_filepath = golden_dirpath / "boot_screen.ppm";
    if (std::filesystem::exists(golden_filepath)) {
        std::vector<uint32_t> golden;
        size_t golden_w, golden_h;

        REQUIRE(read_ppm(golden_filepath, golden, golden_w, golden_h));
        REQUIRE(golden_w == fb.width());
        REQUIRE(golden_h == fb.height());

        size_t diff = compare_frames_exact(frame.data(), fb.stride_pixels(),
                                           golden.data(),
                                           fb.width(), fb.height());
        INFO("Pixel difference: " << diff);
        CHECK(diff == 0);
    } else {
        auto output_filepath = golden_dirpath / "boot_screen_candidate.ppm";
        REQUIRE(write_ppm(output_filepath, frame.data(), fb.width(), fb.height(), fb.stride_pixels()));
        WARN("Golden master not found. Candidate saved to: " << output_filepath);
    }
}

// ============================================================================
// MODE 6 Golden Master Test
// ============================================================================

TEMPLATE_TEST_CASE("MODE 6 boot screen golden master", "[mode6][golden][bitmap]",
                   ModelB, ModelBPlus) {
    // MODE 6: 40x25 text, 8x10 character cells (with gap scanlines)
    // Uses slow clock (1MHz CRTC), 320x250 pixels, 2 colors
    // Screen memory at 0x6000-0x7FFF (8KB)
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();

    // Set startup screen mode to 6 BEFORE reset (uses startup links)
    machine.memory().set_screen_mode(6);
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Boot to BASIC - machine will start in Mode 6
    bool boot_complete = false;
    for (uint64_t i = 0; i < 4'000'000 && !boot_complete; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }

        // Check startUpOptions at $028F - bits 0-2 contain screen mode
        if (i > 2'000'000) {
            uint8_t startup_opts = machine.read(0x028F);
            if ((startup_opts & 0x07) == 6) {
                boot_complete = true;
            }
        }
    }

    // Verify we're in Mode 6 (not teletext)
    REQUIRE_FALSE(machine.memory().video_ula.teletext_mode());

    // Verify startup options shows Mode 6
    uint8_t actual_screen_mode = machine.read(0x028F) & 0x07;
    REQUIRE(actual_screen_mode == 6);

    // Force cursor to steady (non-blinking) mode for deterministic capture
    force_steady_cursor(machine);

    // Run additional frames for stable output
    for (int frame = 0; frame < 4; ++frame) {
        for (int i = 0; i < 80000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }

    auto frame = fb.read_frame();
    auto golden_dirpath = golden_dir<TestType>("mode6");
    std::filesystem::create_directories(golden_dirpath);

    // Check for golden master
    auto golden_filepath = golden_dirpath / "boot_screen.ppm";
    if (std::filesystem::exists(golden_filepath)) {
        std::vector<uint32_t> golden;
        size_t golden_w, golden_h;

        REQUIRE(read_ppm(golden_filepath, golden, golden_w, golden_h));
        REQUIRE(golden_w == fb.width());
        REQUIRE(golden_h == fb.height());

        size_t diff = compare_frames_exact(frame.data(), fb.stride_pixels(),
                                           golden.data(),
                                           fb.width(), fb.height());
        INFO("Pixel difference: " << diff);
        CHECK(diff == 0);
    } else {
        auto output_filepath = golden_dirpath / "boot_screen_candidate.ppm";
        REQUIRE(write_ppm(output_filepath, frame.data(), fb.width(), fb.height(), fb.stride_pixels()));
        WARN("Golden master not found. Candidate saved to: " << output_filepath);
    }
}

// ============================================================================
// MODE 7 Golden Master Test (Teletext)
// ============================================================================

TEMPLATE_TEST_CASE("MODE 7 boot screen golden master", "[mode7][golden][teletext]",
                   ModelB, ModelBPlus) {
    // MODE 7: 40x25 teletext, rendered by SAA5050
    // Screen memory at 0x7C00-0x7FFF (1KB)
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();

    // Mode 7 is the default startup mode (no need to set startup links)
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Boot to BASIC - machine will start in Mode 7 (default)
    bool boot_complete = false;
    for (uint64_t i = 0; i < 4'000'000 && !boot_complete; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }

        // Check startUpOptions at $028F - bits 0-2 contain screen mode
        if (i > 2'000'000) {
            uint8_t startup_opts = machine.read(0x028F);
            if ((startup_opts & 0x07) == 7) {
                boot_complete = true;
            }
        }
    }

    // Verify we're in Mode 7 (teletext)
    REQUIRE(machine.memory().video_ula.teletext_mode());

    // Verify startup options shows Mode 7
    uint8_t actual_screen_mode = machine.read(0x028F) & 0x07;
    REQUIRE(actual_screen_mode == 7);

    // Force cursor to steady (non-blinking) mode for deterministic capture
    force_steady_cursor(machine);

    // Run additional frames for stable output
    for (int frame = 0; frame < 4; ++frame) {
        for (int i = 0; i < 80000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }

    auto frame = fb.read_frame();
    auto golden_dirpath = golden_dir<TestType>("mode7");
    std::filesystem::create_directories(golden_dirpath);

    // Check for golden master
    auto golden_filepath = golden_dirpath / "boot_screen.ppm";
    if (std::filesystem::exists(golden_filepath)) {
        std::vector<uint32_t> golden;
        size_t golden_w, golden_h;

        REQUIRE(read_ppm(golden_filepath, golden, golden_w, golden_h));
        REQUIRE(golden_w == fb.width());
        REQUIRE(golden_h == fb.height());

        size_t diff = compare_frames_exact(frame.data(), fb.stride_pixels(),
                                           golden.data(),
                                           fb.width(), fb.height());
        INFO("Pixel difference: " << diff);
        CHECK(diff == 0);
    } else {
        auto output_filepath = golden_dirpath / "boot_screen_candidate.ppm";
        REQUIRE(write_ppm(output_filepath, frame.data(), fb.width(), fb.height(), fb.stride_pixels()));
        WARN("Golden master not found. Candidate saved to: " << output_filepath);
    }
}

// ============================================================================
// Printable Characters Testcard Tests - Modes 0-6
// ============================================================================

TEMPLATE_TEST_CASE("MODE 0 printable characters testcard", "[mode0][testcard][characters][golden]",
                   ModelB, ModelBPlus) {
    // MODE 0: 80x32 characters
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.memory().set_screen_mode(0);
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Boot to BASIC
    bool boot_complete = false;
    for (uint64_t i = 0; i < 4'000'000 && !boot_complete; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        if (i > 2'000'000 && (machine.read(0x028F) & 0x07) == 0) {
            boot_complete = true;
        }
    }
    REQUIRE(boot_complete);

    // Wait for BASIC to be fully ready
    for (int i = 0; i < 200000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    // Type and run the testcard program (80 cols, 32 rows)
    type_printable_chars_testcard(machine, renderer, 79, 31);

    // Force steady cursor for deterministic capture
    force_steady_cursor(machine);

    // Render frames for stable output
    for (int frame = 0; frame < 4; ++frame) {
        for (int i = 0; i < 80000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }

    auto frame = fb.read_frame();
    auto golden_dirpath = golden_dir<TestType>("mode0");
    std::filesystem::create_directories(golden_dirpath);

    auto golden_filepath = golden_dirpath / "testcard_printable_chars.ppm";
    if (std::filesystem::exists(golden_filepath)) {
        std::vector<uint32_t> golden;
        size_t golden_w, golden_h;

        REQUIRE(read_ppm(golden_filepath, golden, golden_w, golden_h));
        REQUIRE(golden_w == fb.width());
        REQUIRE(golden_h == fb.height());

        size_t diff = compare_frames_exact(frame.data(), fb.stride_pixels(),
                                           golden.data(), fb.width(), fb.height());
        INFO("Pixel difference: " << diff);
        CHECK(diff == 0);
    } else {
        auto output_filepath = golden_dirpath / "testcard_printable_chars_candidate.ppm";
        REQUIRE(write_ppm(output_filepath, frame.data(), fb.width(), fb.height(), fb.stride_pixels()));
        WARN("Golden master not found. Candidate saved to: " << output_filepath);
    }
}

TEMPLATE_TEST_CASE("MODE 1 printable characters testcard", "[mode1][testcard][characters][golden]",
                   ModelB, ModelBPlus) {
    // MODE 1: 40x32 characters
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.memory().set_screen_mode(1);
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Boot to BASIC
    bool boot_complete = false;
    for (uint64_t i = 0; i < 4'000'000 && !boot_complete; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        if (i > 2'000'000 && (machine.read(0x028F) & 0x07) == 1) {
            boot_complete = true;
        }
    }
    REQUIRE(boot_complete);

    // Wait for BASIC to be fully ready
    for (int i = 0; i < 200000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    // Type and run the testcard program (40 cols, 32 rows)
    type_printable_chars_testcard(machine, renderer, 39, 31);

    // Force steady cursor for deterministic capture
    force_steady_cursor(machine);

    // Render frames for stable output
    for (int frame = 0; frame < 4; ++frame) {
        for (int i = 0; i < 80000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }

    auto frame = fb.read_frame();
    auto golden_dirpath = golden_dir<TestType>("mode1");
    std::filesystem::create_directories(golden_dirpath);

    auto golden_filepath = golden_dirpath / "testcard_printable_chars.ppm";
    if (std::filesystem::exists(golden_filepath)) {
        std::vector<uint32_t> golden;
        size_t golden_w, golden_h;

        REQUIRE(read_ppm(golden_filepath, golden, golden_w, golden_h));
        REQUIRE(golden_w == fb.width());
        REQUIRE(golden_h == fb.height());

        size_t diff = compare_frames_exact(frame.data(), fb.stride_pixels(),
                                           golden.data(), fb.width(), fb.height());
        INFO("Pixel difference: " << diff);
        CHECK(diff == 0);
    } else {
        auto output_filepath = golden_dirpath / "testcard_printable_chars_candidate.ppm";
        REQUIRE(write_ppm(output_filepath, frame.data(), fb.width(), fb.height(), fb.stride_pixels()));
        WARN("Golden master not found. Candidate saved to: " << output_filepath);
    }
}

TEMPLATE_TEST_CASE("MODE 2 printable characters testcard", "[mode2][testcard][characters][golden]",
                   ModelB, ModelBPlus) {
    // MODE 2: 20x32 characters
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.memory().set_screen_mode(2);
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Boot to BASIC
    bool boot_complete = false;
    for (uint64_t i = 0; i < 4'000'000 && !boot_complete; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        if (i > 2'000'000 && (machine.read(0x028F) & 0x07) == 2) {
            boot_complete = true;
        }
    }
    REQUIRE(boot_complete);

    // Wait for BASIC to be fully ready
    for (int i = 0; i < 200000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    // Type and run the testcard program (20 cols, 32 rows)
    type_printable_chars_testcard(machine, renderer, 19, 31);

    // Force steady cursor for deterministic capture
    force_steady_cursor(machine);

    // Render frames for stable output
    for (int frame = 0; frame < 4; ++frame) {
        for (int i = 0; i < 80000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }

    auto frame = fb.read_frame();
    auto golden_dirpath = golden_dir<TestType>("mode2");
    std::filesystem::create_directories(golden_dirpath);

    auto golden_filepath = golden_dirpath / "testcard_printable_chars.ppm";
    if (std::filesystem::exists(golden_filepath)) {
        std::vector<uint32_t> golden;
        size_t golden_w, golden_h;

        REQUIRE(read_ppm(golden_filepath, golden, golden_w, golden_h));
        REQUIRE(golden_w == fb.width());
        REQUIRE(golden_h == fb.height());

        size_t diff = compare_frames_exact(frame.data(), fb.stride_pixels(),
                                           golden.data(), fb.width(), fb.height());
        INFO("Pixel difference: " << diff);
        CHECK(diff == 0);
    } else {
        auto output_filepath = golden_dirpath / "testcard_printable_chars_candidate.ppm";
        REQUIRE(write_ppm(output_filepath, frame.data(), fb.width(), fb.height(), fb.stride_pixels()));
        WARN("Golden master not found. Candidate saved to: " << output_filepath);
    }
}

TEMPLATE_TEST_CASE("MODE 3 printable characters testcard", "[mode3][testcard][characters][golden]",
                   ModelB, ModelBPlus) {
    // MODE 3: 80x25 characters
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.memory().set_screen_mode(3);
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Boot to BASIC
    bool boot_complete = false;
    for (uint64_t i = 0; i < 4'000'000 && !boot_complete; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        if (i > 2'000'000 && (machine.read(0x028F) & 0x07) == 3) {
            boot_complete = true;
        }
    }
    REQUIRE(boot_complete);

    // Wait for BASIC to be fully ready
    for (int i = 0; i < 200000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    // Type and run the testcard program (80 cols, 25 rows)
    type_printable_chars_testcard(machine, renderer, 79, 24);

    // Force steady cursor for deterministic capture
    force_steady_cursor(machine);

    // Render frames for stable output
    for (int frame = 0; frame < 4; ++frame) {
        for (int i = 0; i < 80000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }

    auto frame = fb.read_frame();
    auto golden_dirpath = golden_dir<TestType>("mode3");
    std::filesystem::create_directories(golden_dirpath);

    auto golden_filepath = golden_dirpath / "testcard_printable_chars.ppm";
    if (std::filesystem::exists(golden_filepath)) {
        std::vector<uint32_t> golden;
        size_t golden_w, golden_h;

        REQUIRE(read_ppm(golden_filepath, golden, golden_w, golden_h));
        REQUIRE(golden_w == fb.width());
        REQUIRE(golden_h == fb.height());

        size_t diff = compare_frames_exact(frame.data(), fb.stride_pixels(),
                                           golden.data(), fb.width(), fb.height());
        INFO("Pixel difference: " << diff);
        CHECK(diff == 0);
    } else {
        auto output_filepath = golden_dirpath / "testcard_printable_chars_candidate.ppm";
        REQUIRE(write_ppm(output_filepath, frame.data(), fb.width(), fb.height(), fb.stride_pixels()));
        WARN("Golden master not found. Candidate saved to: " << output_filepath);
    }
}

TEMPLATE_TEST_CASE("MODE 4 printable characters testcard", "[mode4][testcard][characters][golden]",
                   ModelB, ModelBPlus) {
    // MODE 4: 40x32 characters
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.memory().set_screen_mode(4);
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Boot to BASIC
    bool boot_complete = false;
    for (uint64_t i = 0; i < 4'000'000 && !boot_complete; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        if (i > 2'000'000 && (machine.read(0x028F) & 0x07) == 4) {
            boot_complete = true;
        }
    }
    REQUIRE(boot_complete);

    // Wait for BASIC to be fully ready
    for (int i = 0; i < 200000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    // Type and run the testcard program (40 cols, 32 rows)
    type_printable_chars_testcard(machine, renderer, 39, 31);

    // Force steady cursor for deterministic capture
    force_steady_cursor(machine);

    // Render frames for stable output
    for (int frame = 0; frame < 4; ++frame) {
        for (int i = 0; i < 80000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }

    auto frame = fb.read_frame();
    auto golden_dirpath = golden_dir<TestType>("mode4");
    std::filesystem::create_directories(golden_dirpath);

    auto golden_filepath = golden_dirpath / "testcard_printable_chars.ppm";
    if (std::filesystem::exists(golden_filepath)) {
        std::vector<uint32_t> golden;
        size_t golden_w, golden_h;

        REQUIRE(read_ppm(golden_filepath, golden, golden_w, golden_h));
        REQUIRE(golden_w == fb.width());
        REQUIRE(golden_h == fb.height());

        size_t diff = compare_frames_exact(frame.data(), fb.stride_pixels(),
                                           golden.data(), fb.width(), fb.height());
        INFO("Pixel difference: " << diff);
        CHECK(diff == 0);
    } else {
        auto output_filepath = golden_dirpath / "testcard_printable_chars_candidate.ppm";
        REQUIRE(write_ppm(output_filepath, frame.data(), fb.width(), fb.height(), fb.stride_pixels()));
        WARN("Golden master not found. Candidate saved to: " << output_filepath);
    }
}

TEMPLATE_TEST_CASE("MODE 5 printable characters testcard", "[mode5][testcard][characters][golden]",
                   ModelB, ModelBPlus) {
    // MODE 5: 20x32 characters
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.memory().set_screen_mode(5);
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Boot to BASIC
    bool boot_complete = false;
    for (uint64_t i = 0; i < 4'000'000 && !boot_complete; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        if (i > 2'000'000 && (machine.read(0x028F) & 0x07) == 5) {
            boot_complete = true;
        }
    }
    REQUIRE(boot_complete);

    // Wait for BASIC to be fully ready
    for (int i = 0; i < 200000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    // Type and run the testcard program (20 cols, 32 rows)
    type_printable_chars_testcard(machine, renderer, 19, 31);

    // Force steady cursor for deterministic capture
    force_steady_cursor(machine);

    // Render frames for stable output
    for (int frame = 0; frame < 4; ++frame) {
        for (int i = 0; i < 80000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }

    auto frame = fb.read_frame();
    auto golden_dirpath = golden_dir<TestType>("mode5");
    std::filesystem::create_directories(golden_dirpath);

    auto golden_filepath = golden_dirpath / "testcard_printable_chars.ppm";
    if (std::filesystem::exists(golden_filepath)) {
        std::vector<uint32_t> golden;
        size_t golden_w, golden_h;

        REQUIRE(read_ppm(golden_filepath, golden, golden_w, golden_h));
        REQUIRE(golden_w == fb.width());
        REQUIRE(golden_h == fb.height());

        size_t diff = compare_frames_exact(frame.data(), fb.stride_pixels(),
                                           golden.data(), fb.width(), fb.height());
        INFO("Pixel difference: " << diff);
        CHECK(diff == 0);
    } else {
        auto output_filepath = golden_dirpath / "testcard_printable_chars_candidate.ppm";
        REQUIRE(write_ppm(output_filepath, frame.data(), fb.width(), fb.height(), fb.stride_pixels()));
        WARN("Golden master not found. Candidate saved to: " << output_filepath);
    }
}

TEMPLATE_TEST_CASE("MODE 6 printable characters testcard", "[mode6][testcard][characters][golden]",
                   ModelB, ModelBPlus) {
    // MODE 6: 40x25 characters
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.memory().set_screen_mode(6);
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Boot to BASIC
    bool boot_complete = false;
    for (uint64_t i = 0; i < 4'000'000 && !boot_complete; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        if (i > 2'000'000 && (machine.read(0x028F) & 0x07) == 6) {
            boot_complete = true;
        }
    }
    REQUIRE(boot_complete);

    // Wait for BASIC to be fully ready
    for (int i = 0; i < 200000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    // Type and run the testcard program (40 cols, 25 rows)
    type_printable_chars_testcard(machine, renderer, 39, 24);

    // Force steady cursor for deterministic capture
    force_steady_cursor(machine);

    // Render frames for stable output
    for (int frame = 0; frame < 4; ++frame) {
        for (int i = 0; i < 80000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }

    auto frame = fb.read_frame();
    auto golden_dirpath = golden_dir<TestType>("mode6");
    std::filesystem::create_directories(golden_dirpath);

    auto golden_filepath = golden_dirpath / "testcard_printable_chars.ppm";
    if (std::filesystem::exists(golden_filepath)) {
        std::vector<uint32_t> golden;
        size_t golden_w, golden_h;

        REQUIRE(read_ppm(golden_filepath, golden, golden_w, golden_h));
        REQUIRE(golden_w == fb.width());
        REQUIRE(golden_h == fb.height());

        size_t diff = compare_frames_exact(frame.data(), fb.stride_pixels(),
                                           golden.data(), fb.width(), fb.height());
        INFO("Pixel difference: " << diff);
        CHECK(diff == 0);
    } else {
        auto output_filepath = golden_dirpath / "testcard_printable_chars_candidate.ppm";
        REQUIRE(write_ppm(output_filepath, frame.data(), fb.width(), fb.height(), fb.stride_pixels()));
        WARN("Golden master not found. Candidate saved to: " << output_filepath);
    }
}

// ============================================================================
// VDU 19 Blue Background Tests - Modes 0-6
// ============================================================================
// These tests issue VDU 19,0,4,0,0,0 to change the background color to blue.
// In modes 3 and 6 (25-row text modes), the empty scanlines between text
// lines remain black, creating an interesting "lined paper" effect.

TEMPLATE_TEST_CASE("MODE 0 blue background", "[mode0][vdu19][background][golden]",
                   ModelB, ModelBPlus) {
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.memory().set_screen_mode(0);
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Boot to BASIC
    bool boot_complete = false;
    for (uint64_t i = 0; i < 4'000'000 && !boot_complete; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        if (i > 2'000'000 && (machine.read(0x028F) & 0x07) == 0) {
            boot_complete = true;
        }
    }
    REQUIRE(boot_complete);

    // Wait for BASIC to be fully ready
    for (int i = 0; i < 200000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    // Type VDU command to change background to blue
    type_string_with_shift(machine, renderer, "VDU 19,0,4,0,0,0\r", 100000);

    // Wait for screen redraw
    for (int i = 0; i < 500000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    force_steady_cursor(machine);

    // Render frames for stable output
    for (int frame = 0; frame < 4; ++frame) {
        for (int i = 0; i < 80000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }

    auto frame = fb.read_frame();
    auto golden_dirpath = golden_dir<TestType>("mode0");
    std::filesystem::create_directories(golden_dirpath);

    auto golden_filepath = golden_dirpath / "blue_background.ppm";
    if (std::filesystem::exists(golden_filepath)) {
        std::vector<uint32_t> golden;
        size_t golden_w, golden_h;

        REQUIRE(read_ppm(golden_filepath, golden, golden_w, golden_h));
        REQUIRE(golden_w == fb.width());
        REQUIRE(golden_h == fb.height());

        size_t diff = compare_frames_exact(frame.data(), fb.stride_pixels(),
                                           golden.data(), fb.width(), fb.height());
        INFO("Pixel difference: " << diff);
        CHECK(diff == 0);
    } else {
        auto output_filepath = golden_dirpath / "blue_background_candidate.ppm";
        REQUIRE(write_ppm(output_filepath, frame.data(), fb.width(), fb.height(), fb.stride_pixels()));
        WARN("Golden master not found. Candidate saved to: " << output_filepath);
    }
}

TEMPLATE_TEST_CASE("MODE 1 blue background", "[mode1][vdu19][background][golden]",
                   ModelB, ModelBPlus) {
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.memory().set_screen_mode(1);
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    bool boot_complete = false;
    for (uint64_t i = 0; i < 4'000'000 && !boot_complete; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        if (i > 2'000'000 && (machine.read(0x028F) & 0x07) == 1) {
            boot_complete = true;
        }
    }
    REQUIRE(boot_complete);

    for (int i = 0; i < 200000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    type_string_with_shift(machine, renderer, "VDU 19,0,4,0,0,0\r", 100000);

    for (int i = 0; i < 500000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    force_steady_cursor(machine);

    for (int frame = 0; frame < 4; ++frame) {
        for (int i = 0; i < 80000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }

    auto frame = fb.read_frame();
    auto golden_dirpath = golden_dir<TestType>("mode1");
    std::filesystem::create_directories(golden_dirpath);

    auto golden_filepath = golden_dirpath / "blue_background.ppm";
    if (std::filesystem::exists(golden_filepath)) {
        std::vector<uint32_t> golden;
        size_t golden_w, golden_h;

        REQUIRE(read_ppm(golden_filepath, golden, golden_w, golden_h));
        REQUIRE(golden_w == fb.width());
        REQUIRE(golden_h == fb.height());

        size_t diff = compare_frames_exact(frame.data(), fb.stride_pixels(),
                                           golden.data(), fb.width(), fb.height());
        INFO("Pixel difference: " << diff);
        CHECK(diff == 0);
    } else {
        auto output_filepath = golden_dirpath / "blue_background_candidate.ppm";
        REQUIRE(write_ppm(output_filepath, frame.data(), fb.width(), fb.height(), fb.stride_pixels()));
        WARN("Golden master not found. Candidate saved to: " << output_filepath);
    }
}

TEMPLATE_TEST_CASE("MODE 2 blue background", "[mode2][vdu19][background][golden]",
                   ModelB, ModelBPlus) {
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.memory().set_screen_mode(2);
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    bool boot_complete = false;
    for (uint64_t i = 0; i < 4'000'000 && !boot_complete; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        if (i > 2'000'000 && (machine.read(0x028F) & 0x07) == 2) {
            boot_complete = true;
        }
    }
    REQUIRE(boot_complete);

    for (int i = 0; i < 200000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    type_string_with_shift(machine, renderer, "VDU 19,0,4,0,0,0\r", 100000);

    for (int i = 0; i < 500000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    force_steady_cursor(machine);

    for (int frame = 0; frame < 4; ++frame) {
        for (int i = 0; i < 80000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }

    auto frame = fb.read_frame();
    auto golden_dirpath = golden_dir<TestType>("mode2");
    std::filesystem::create_directories(golden_dirpath);

    auto golden_filepath = golden_dirpath / "blue_background.ppm";
    if (std::filesystem::exists(golden_filepath)) {
        std::vector<uint32_t> golden;
        size_t golden_w, golden_h;

        REQUIRE(read_ppm(golden_filepath, golden, golden_w, golden_h));
        REQUIRE(golden_w == fb.width());
        REQUIRE(golden_h == fb.height());

        size_t diff = compare_frames_exact(frame.data(), fb.stride_pixels(),
                                           golden.data(), fb.width(), fb.height());
        INFO("Pixel difference: " << diff);
        CHECK(diff == 0);
    } else {
        auto output_filepath = golden_dirpath / "blue_background_candidate.ppm";
        REQUIRE(write_ppm(output_filepath, frame.data(), fb.width(), fb.height(), fb.stride_pixels()));
        WARN("Golden master not found. Candidate saved to: " << output_filepath);
    }
}

TEMPLATE_TEST_CASE("MODE 3 blue background", "[mode3][vdu19][background][golden]",
                   ModelB, ModelBPlus) {
    // MODE 3: 80x25 - empty scanlines between text rows create "lined paper" effect
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.memory().set_screen_mode(3);
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    bool boot_complete = false;
    for (uint64_t i = 0; i < 4'000'000 && !boot_complete; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        if (i > 2'000'000 && (machine.read(0x028F) & 0x07) == 3) {
            boot_complete = true;
        }
    }
    REQUIRE(boot_complete);

    for (int i = 0; i < 200000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    type_string_with_shift(machine, renderer, "VDU 19,0,4,0,0,0\r", 100000);

    for (int i = 0; i < 500000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    force_steady_cursor(machine);

    for (int frame = 0; frame < 4; ++frame) {
        for (int i = 0; i < 80000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }

    auto frame = fb.read_frame();
    auto golden_dirpath = golden_dir<TestType>("mode3");
    std::filesystem::create_directories(golden_dirpath);

    auto golden_filepath = golden_dirpath / "blue_background.ppm";
    if (std::filesystem::exists(golden_filepath)) {
        std::vector<uint32_t> golden;
        size_t golden_w, golden_h;

        REQUIRE(read_ppm(golden_filepath, golden, golden_w, golden_h));
        REQUIRE(golden_w == fb.width());
        REQUIRE(golden_h == fb.height());

        size_t diff = compare_frames_exact(frame.data(), fb.stride_pixels(),
                                           golden.data(), fb.width(), fb.height());
        INFO("Pixel difference: " << diff);
        CHECK(diff == 0);
    } else {
        auto output_filepath = golden_dirpath / "blue_background_candidate.ppm";
        REQUIRE(write_ppm(output_filepath, frame.data(), fb.width(), fb.height(), fb.stride_pixels()));
        WARN("Golden master not found. Candidate saved to: " << output_filepath);
    }
}

TEMPLATE_TEST_CASE("MODE 4 blue background", "[mode4][vdu19][background][golden]",
                   ModelB, ModelBPlus) {
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.memory().set_screen_mode(4);
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    bool boot_complete = false;
    for (uint64_t i = 0; i < 4'000'000 && !boot_complete; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        if (i > 2'000'000 && (machine.read(0x028F) & 0x07) == 4) {
            boot_complete = true;
        }
    }
    REQUIRE(boot_complete);

    for (int i = 0; i < 200000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    type_string_with_shift(machine, renderer, "VDU 19,0,4,0,0,0\r", 100000);

    for (int i = 0; i < 500000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    force_steady_cursor(machine);

    for (int frame = 0; frame < 4; ++frame) {
        for (int i = 0; i < 80000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }

    auto frame = fb.read_frame();
    auto golden_dirpath = golden_dir<TestType>("mode4");
    std::filesystem::create_directories(golden_dirpath);

    auto golden_filepath = golden_dirpath / "blue_background.ppm";
    if (std::filesystem::exists(golden_filepath)) {
        std::vector<uint32_t> golden;
        size_t golden_w, golden_h;

        REQUIRE(read_ppm(golden_filepath, golden, golden_w, golden_h));
        REQUIRE(golden_w == fb.width());
        REQUIRE(golden_h == fb.height());

        size_t diff = compare_frames_exact(frame.data(), fb.stride_pixels(),
                                           golden.data(), fb.width(), fb.height());
        INFO("Pixel difference: " << diff);
        CHECK(diff == 0);
    } else {
        auto output_filepath = golden_dirpath / "blue_background_candidate.ppm";
        REQUIRE(write_ppm(output_filepath, frame.data(), fb.width(), fb.height(), fb.stride_pixels()));
        WARN("Golden master not found. Candidate saved to: " << output_filepath);
    }
}

TEMPLATE_TEST_CASE("MODE 5 blue background", "[mode5][vdu19][background][golden]",
                   ModelB, ModelBPlus) {
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.memory().set_screen_mode(5);
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    bool boot_complete = false;
    for (uint64_t i = 0; i < 4'000'000 && !boot_complete; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        if (i > 2'000'000 && (machine.read(0x028F) & 0x07) == 5) {
            boot_complete = true;
        }
    }
    REQUIRE(boot_complete);

    for (int i = 0; i < 200000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    type_string_with_shift(machine, renderer, "VDU 19,0,4,0,0,0\r", 100000);

    for (int i = 0; i < 500000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    force_steady_cursor(machine);

    for (int frame = 0; frame < 4; ++frame) {
        for (int i = 0; i < 80000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }

    auto frame = fb.read_frame();
    auto golden_dirpath = golden_dir<TestType>("mode5");
    std::filesystem::create_directories(golden_dirpath);

    auto golden_filepath = golden_dirpath / "blue_background.ppm";
    if (std::filesystem::exists(golden_filepath)) {
        std::vector<uint32_t> golden;
        size_t golden_w, golden_h;

        REQUIRE(read_ppm(golden_filepath, golden, golden_w, golden_h));
        REQUIRE(golden_w == fb.width());
        REQUIRE(golden_h == fb.height());

        size_t diff = compare_frames_exact(frame.data(), fb.stride_pixels(),
                                           golden.data(), fb.width(), fb.height());
        INFO("Pixel difference: " << diff);
        CHECK(diff == 0);
    } else {
        auto output_filepath = golden_dirpath / "blue_background_candidate.ppm";
        REQUIRE(write_ppm(output_filepath, frame.data(), fb.width(), fb.height(), fb.stride_pixels()));
        WARN("Golden master not found. Candidate saved to: " << output_filepath);
    }
}

TEMPLATE_TEST_CASE("MODE 6 blue background", "[mode6][vdu19][background][golden]",
                   ModelB, ModelBPlus) {
    // MODE 6: 40x25 - empty scanlines between text rows create "lined paper" effect
    REQUIRE(roms_available<TestType>());

    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.memory().set_screen_mode(6);
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    bool boot_complete = false;
    for (uint64_t i = 0; i < 4'000'000 && !boot_complete; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        if (i > 2'000'000 && (machine.read(0x028F) & 0x07) == 6) {
            boot_complete = true;
        }
    }
    REQUIRE(boot_complete);

    for (int i = 0; i < 200000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    type_string_with_shift(machine, renderer, "VDU 19,0,4,0,0,0\r", 100000);

    for (int i = 0; i < 500000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    force_steady_cursor(machine);

    for (int frame = 0; frame < 4; ++frame) {
        for (int i = 0; i < 80000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }

    auto frame = fb.read_frame();
    auto golden_dirpath = golden_dir<TestType>("mode6");
    std::filesystem::create_directories(golden_dirpath);

    auto golden_filepath = golden_dirpath / "blue_background.ppm";
    if (std::filesystem::exists(golden_filepath)) {
        std::vector<uint32_t> golden;
        size_t golden_w, golden_h;

        REQUIRE(read_ppm(golden_filepath, golden, golden_w, golden_h));
        REQUIRE(golden_w == fb.width());
        REQUIRE(golden_h == fb.height());

        size_t diff = compare_frames_exact(frame.data(), fb.stride_pixels(),
                                           golden.data(), fb.width(), fb.height());
        INFO("Pixel difference: " << diff);
        CHECK(diff == 0);
    } else {
        auto output_filepath = golden_dirpath / "blue_background_candidate.ppm";
        REQUIRE(write_ppm(output_filepath, frame.data(), fb.width(), fb.height(), fb.stride_pixels()));
        WARN("Golden master not found. Candidate saved to: " << output_filepath);
    }
}

// ============================================================================
// AUG MODE 8 Tests (Advanced User Guide custom mode)
// 80x256 pixels, 16 colors, 10 columns x 32 rows
// ============================================================================

TEMPLATE_TEST_CASE("AUG MODE 8 blue background", "[aug_mode8][vdu19][background][golden]",
                   ModelB, ModelBPlus) {

    REQUIRE(roms_available<TestType>());
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);

    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    REQUIRE(mos_rom.size() == 16384);

    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");
    REQUIRE(basic_rom.size() == 16384);

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.memory().set_screen_mode(5);  // Start in MODE 5
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    bool boot_complete = false;
    for (uint64_t i = 0; i < 4'000'000 && !boot_complete; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        if (i > 2'000'000 && (machine.read(0x028F) & 0x07) == 5) {
            boot_complete = true;
        }
    }
    REQUIRE(boot_complete);

    for (int i = 0; i < 200000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    // Set up the custom AUG MODE 8
    setup_aug_mode8(machine, renderer);

    // Change background to blue
    type_string_with_shift(machine, renderer, "VDU 19,0,4,0,0,0\r", 100000);

    for (int i = 0; i < 500000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    force_steady_cursor(machine);

    for (int frame = 0; frame < 4; ++frame) {
        for (int i = 0; i < 80000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }

    auto frame = fb.read_frame();
    auto golden_dirpath = golden_dir<TestType>("aug_mode8");
    std::filesystem::create_directories(golden_dirpath);

    auto golden_filepath = golden_dirpath / "blue_background.ppm";
    if (std::filesystem::exists(golden_filepath)) {
        std::vector<uint32_t> golden;
        size_t golden_w, golden_h;

        REQUIRE(read_ppm(golden_filepath, golden, golden_w, golden_h));
        REQUIRE(golden_w == fb.width());
        REQUIRE(golden_h == fb.height());

        size_t diff = compare_frames_exact(frame.data(), fb.stride_pixels(),
                                           golden.data(), fb.width(), fb.height());
        INFO("Pixel difference: " << diff);
        CHECK(diff == 0);
    } else {
        auto output_filepath = golden_dirpath / "blue_background_candidate.ppm";
        REQUIRE(write_ppm(output_filepath, frame.data(), fb.width(), fb.height(), fb.stride_pixels()));
        WARN("Golden master not found. Candidate saved to: " << output_filepath);
    }
}

TEMPLATE_TEST_CASE("AUG MODE 8 printable characters testcard", "[aug_mode8][testcard][characters][golden]",
                   ModelB, ModelBPlus) {
    // AUG MODE 8: 10 columns x 32 rows

    REQUIRE(roms_available<TestType>());
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);

    auto mos_rom = load_rom(rom_dirpath / RomConfig<TestType>::mos_filename);
    REQUIRE(mos_rom.size() == 16384);

    auto basic_rom = load_rom(rom_dirpath / "bbc-basic_2.rom");
    REQUIRE(basic_rom.size() == 16384);

    TestType machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.memory().set_screen_mode(5);  // Start in MODE 5
    machine.reset();

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    bool boot_complete = false;
    for (uint64_t i = 0; i < 4'000'000 && !boot_complete; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
        if (i > 2'000'000 && (machine.read(0x028F) & 0x07) == 5) {
            boot_complete = true;
        }
    }
    REQUIRE(boot_complete);

    for (int i = 0; i < 200000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    // Set up the custom AUG MODE 8
    setup_aug_mode8(machine, renderer);

    // Type the printable chars testcard: 10 cols x 32 rows => max_col=9, max_row=31
    type_printable_chars_testcard(machine, renderer, 9, 31);

    force_steady_cursor(machine);

    for (int frame = 0; frame < 4; ++frame) {
        for (int i = 0; i < 80000; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }

    auto frame = fb.read_frame();
    auto golden_dirpath = golden_dir<TestType>("aug_mode8");
    std::filesystem::create_directories(golden_dirpath);

    auto golden_filepath = golden_dirpath / "testcard_printable_chars.ppm";
    if (std::filesystem::exists(golden_filepath)) {
        std::vector<uint32_t> golden;
        size_t golden_w, golden_h;

        REQUIRE(read_ppm(golden_filepath, golden, golden_w, golden_h));
        REQUIRE(golden_w == fb.width());
        REQUIRE(golden_h == fb.height());

        size_t diff = compare_frames_exact(frame.data(), fb.stride_pixels(),
                                           golden.data(), fb.width(), fb.height());
        INFO("Pixel difference: " << diff);
        CHECK(diff == 0);
    } else {
        auto output_filepath = golden_dirpath / "testcard_printable_chars_candidate.ppm";
        REQUIRE(write_ppm(output_filepath, frame.data(), fb.width(), fb.height(), fb.stride_pixels()));
        WARN("Golden master not found. Candidate saved to: " << output_filepath);
    }
}

#endif // BEEBIUM_ROM_DIR
