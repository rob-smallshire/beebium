// test_video_ula.cpp
// Behaviour tests for Video ULA
//
// Tests the externally observable pixel output:
// - Palette mapping: input byte -> output colors for each mode
// - Mode switching effects on pixel width
// - Cursor XOR behaviour
//
// NOT tested: internal shift register state

#include <beebium/devices/VideoUla.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace beebium;

// Helper to compare VideoDataPixel values (ignoring metadata in x field for some tests)
static bool pixels_equal_rgb(const VideoDataPixel& a, const VideoDataPixel& b) {
    return (a.value & 0x0FFF) == (b.value & 0x0FFF);
}

TEST_CASE("VideoUla palette mapping", "[video_ula][palette]") {
    VideoUla ula;
    ula.reset();

    SECTION("palette write sets logical to physical mapping") {
        // Write palette entry: index 5 -> physical color 3
        // Palette format: high nibble = index, low nibble = color (XOR 7)
        // To get physical 3, write value 3 ^ 7 = 4
        ula.write(1, 0x54);  // Index 5 = color XOR 7 -> 4 ^ 7 = 3

        REQUIRE(ula.palette(5) == 3);
    }

    SECTION("output_palette returns correct RGB for each physical color") {
        // Set all 16 logical colors to physical color 0 (black)
        for (uint8_t i = 0; i < 16; ++i) {
            ula.write(1, (i << 4) | 0x07);  // 7 ^ 7 = 0 (black)
        }
        REQUIRE(pixels_equal_rgb(ula.output_palette(0), bbc_colors::BLACK));

        // Set index 1 to physical color 7 (white)
        ula.write(1, 0x10);  // 0 ^ 7 = 7 (white)
        REQUIRE(pixels_equal_rgb(ula.output_palette(1), bbc_colors::WHITE));

        // Set index 2 to physical color 1 (red)
        ula.write(1, 0x26);  // 6 ^ 7 = 1 (red)
        REQUIRE(pixels_equal_rgb(ula.output_palette(2), bbc_colors::RED));
    }

    SECTION("palette XOR with 7 produces expected colors") {
        // The hardware XORs the written value with 7
        // Write 0 -> physical 7 (white)
        ula.write(1, 0x00);  // Index 0, color 0 ^ 7 = 7
        REQUIRE(ula.palette(0) == 7);

        // Write 7 -> physical 0 (black)
        ula.write(1, 0x07);  // Index 0, color 7 ^ 7 = 0
        REQUIRE(ula.palette(0) == 0);
    }
}

TEST_CASE("VideoUla control register", "[video_ula][control]") {
    VideoUla ula;
    ula.reset();

    SECTION("teletext mode controlled by bit 1") {
        REQUIRE(ula.teletext_mode() == false);

        ula.write(0, 0x02);  // Set teletext bit
        REQUIRE(ula.teletext_mode() == true);

        ula.write(0, 0x00);  // Clear teletext bit
        REQUIRE(ula.teletext_mode() == false);
    }

    SECTION("fast clock controlled by bit 4") {
        REQUIRE(ula.fast_clock() == false);

        ula.write(0, 0x10);  // Set fast clock bit
        REQUIRE(ula.fast_clock() == true);

        ula.write(0, 0x00);
        REQUIRE(ula.fast_clock() == false);
    }

    SECTION("line width mode from bits 2-3") {
        ula.write(0, 0x00);  // Width mode 0
        REQUIRE(ula.line_width_mode() == 0);

        ula.write(0, 0x04);  // Width mode 1
        REQUIRE(ula.line_width_mode() == 1);

        ula.write(0, 0x08);  // Width mode 2
        REQUIRE(ula.line_width_mode() == 2);

        ula.write(0, 0x0C);  // Width mode 3
        REQUIRE(ula.line_width_mode() == 3);
    }

    SECTION("flash select from bit 0") {
        REQUIRE(ula.flash_select() == false);

        ula.write(0, 0x01);
        REQUIRE(ula.flash_select() == true);
    }
}

TEST_CASE("VideoUla Mode 0 pixel output (8 pixels per byte)", "[video_ula][pixels]") {
    VideoUla ula;
    ula.reset();

    // Configure for Mode 0: fast_clock=1, line_width=3
    ula.write(0, 0x1C);  // 0x10 (fast) | 0x0C (width 3)

    // BBC pixel format: bits 7,5,3,1 form 4-bit logical color index
    // For byte 0x00: all bits 0 -> index 0
    // For byte 0xFF: all bits 1 -> index 15
    // Set up palette: index 0 = black, index 15 = white
    ula.write(1, 0x07);  // Index 0 -> physical 0 (black): 7 ^ 7 = 0
    ula.write(1, 0xF0);  // Index 15 -> physical 7 (white): 0 ^ 7 = 7

    SECTION("byte 0x00 produces 8 black pixels") {
        ula.byte(0x00, false);
        PixelBatch batch;
        ula.emit_pixels(batch);

        // Check pixel RGB values (ignore x field which gets overwritten)
        for (int i = 0; i < 8; ++i) {
            REQUIRE(pixels_equal_rgb(batch.pixels.pixels[i], bbc_colors::BLACK));
        }
    }

    SECTION("byte 0xFF produces 8 white pixels") {
        ula.byte(0xFF, false);
        PixelBatch batch;
        ula.emit_pixels(batch);

        for (int i = 0; i < 8; ++i) {
            REQUIRE(pixels_equal_rgb(batch.pixels.pixels[i], bbc_colors::WHITE));
        }
    }

    SECTION("control register state for Mode 0") {
        // Verify the mode configuration is correct
        REQUIRE(ula.fast_clock() == true);
        REQUIRE(ula.line_width_mode() == 3);
        REQUIRE(ula.teletext_mode() == false);
    }
}

TEST_CASE("VideoUla Mode 2 pixel output (2 pixels per byte, doubled to 8)", "[video_ula][pixels]") {
    VideoUla ula;
    ula.reset();

    // Configure for Mode 2: fast_clock=1, line_width=1
    ula.write(0, 0x14);  // 0x10 (fast) | 0x04 (width 1)

    // Set up palette: all 16 colors
    for (uint8_t i = 0; i < 8; ++i) {
        // Map logical 0,2,4,... to physical colors 0,1,2,...
        ula.write(1, ((i * 2) << 4) | ((7 - i) & 0x0F));  // XOR logic
    }

    SECTION("Mode 2 produces 2 logical pixels repeated 4 times each") {
        // Byte 0x00: should produce logical color 0 x4, logical color 0 x4
        ula.byte(0x00, false);
        PixelBatch batch;
        ula.emit_pixels(batch);

        // All 8 pixels should be the same (palette[0])
        VideoDataPixel first = batch.pixels.pixels[0];
        for (int i = 1; i < 8; ++i) {
            REQUIRE(pixels_equal_rgb(batch.pixels.pixels[i], first));
        }
    }
}

TEST_CASE("VideoUla cursor XOR", "[video_ula][cursor]") {
    VideoUla ula;
    ula.reset();

    // Configure for Mode 0
    ula.write(0, 0x1C);

    // Set palette: all black
    for (uint8_t i = 0; i < 16; ++i) {
        ula.write(1, (i << 4) | 0x07);  // All to black
    }

    // Set cursor width bits to non-zero
    ula.write(0, 0x1C | 0x20);  // Width 1

    SECTION("cursor XORs pixels with white") {
        ula.byte(0x00, true);  // cursor_active = true
        PixelBatch batch;
        ula.emit_pixels(batch);

        // Black XOR white = white
        for (int i = 0; i < 8; ++i) {
            REQUIRE(pixels_equal_rgb(batch.pixels.pixels[i], bbc_colors::WHITE));
        }
    }

    SECTION("no cursor without cursor_active") {
        ula.byte(0x00, false);  // cursor_active = false
        PixelBatch batch;
        ula.emit_pixels(batch);

        // Should stay black
        for (int i = 0; i < 8; ++i) {
            REQUIRE(pixels_equal_rgb(batch.pixels.pixels[i], bbc_colors::BLACK));
        }
    }
}

TEST_CASE("VideoUla teletext mode handling", "[video_ula][teletext]") {
    VideoUla ula;
    ula.reset();

    // Enable teletext mode
    ula.write(0, 0x02);

    SECTION("teletext mode sets batch type to Teletext") {
        ula.byte(0x00, false);
        PixelBatch batch;
        ula.emit_pixels(batch);

        REQUIRE(batch.type() == PixelBatchType::Teletext);
    }
}

TEST_CASE("VideoUla blanking output", "[video_ula][blank]") {
    VideoUla ula;
    ula.reset();

    SECTION("emit_blank produces cleared batch") {
        PixelBatch batch;
        // Set some non-zero data first
        batch.pixels.values[0] = 0xFFFFFFFFFFFFFFFF;
        batch.pixels.values[1] = 0xFFFFFFFFFFFFFFFF;

        ula.emit_blank(batch);

        REQUIRE(batch.pixels.values[0] == 0);
        REQUIRE(batch.pixels.values[1] == 0);
        REQUIRE(batch.type() == PixelBatchType::Nothing);
    }
}

TEST_CASE("VideoUla reset", "[video_ula][reset]") {
    VideoUla ula;

    // Configure some state
    ula.write(0, 0xFF);
    ula.write(1, 0x12);

    ula.reset();

    SECTION("reset clears control register") {
        REQUIRE(ula.control() == 0);
        REQUIRE(ula.teletext_mode() == false);
        REQUIRE(ula.fast_clock() == false);
    }

    SECTION("reset clears palette") {
        REQUIRE(ula.palette(0) == 0);
        REQUIRE(ula.palette(1) == 0);
    }
}

TEST_CASE("VideoUla read returns 0xFE", "[video_ula][read]") {
    VideoUla ula;
    ula.reset();

    SECTION("all reads return 0xFE") {
        REQUIRE(ula.read(0) == 0xFE);
        REQUIRE(ula.read(1) == 0xFE);
    }
}
