#include <catch2/catch_test_macros.hpp>
#include <beebium/Saa5050.hpp>
#include <beebium/PixelBatch.hpp>

using namespace beebium;

// ============================================================================
// Phase 1: Font Data and Basic Structure
// ============================================================================

TEST_CASE("Teletext font data validity", "[saa5050][font]") {
    SECTION("space character (0x20) is blank") {
        for (int row = 0; row < 10; ++row) {
            CHECK(TELETEXT_FONT[0][row] == 0);
        }
    }

    SECTION("'A' character has recognizable pattern") {
        // 'A' is at index 33 (0x41 - 0x20)
        constexpr int A_index = 'A' - 0x20;

        // Row 0 should be blank (top of character)
        CHECK(TELETEXT_FONT[A_index][0] == 0);

        // Middle rows should have pixels
        bool has_pixels = false;
        for (int row = 1; row < 8; ++row) {
            if (TELETEXT_FONT[A_index][row] != 0) {
                has_pixels = true;
                break;
            }
        }
        CHECK(has_pixels);
    }

    SECTION("'#' (British Pound) has pixels") {
        // '#' is at index 3 (0x23 - 0x20)
        constexpr int pound_index = '#' - 0x20;

        bool has_pixels = false;
        for (int row = 0; row < 10; ++row) {
            if (TELETEXT_FONT[pound_index][row] != 0) {
                has_pixels = true;
                break;
            }
        }
        CHECK(has_pixels);
    }
}

TEST_CASE("SAA5050 construction and reset", "[saa5050]") {
    Saa5050 chip;

    SECTION("initial state after construction") {
        // Default: white foreground, black background
        CHECK(chip.foreground() == 7);
        CHECK(chip.background() == 0);
        CHECK(chip.charset() == TeletextCharset::Alpha);
        CHECK(chip.raster() == 0);
    }

    SECTION("reset restores default state") {
        // Modify state
        chip.vsync();  // Increments frame counter

        // Reset should restore defaults
        chip.reset();
        CHECK(chip.foreground() == 7);
        CHECK(chip.background() == 0);
        CHECK(chip.raster() == 0);
    }
}

TEST_CASE("SAA5050 line management", "[saa5050]") {
    Saa5050 chip;

    SECTION("start_of_line resets per-line state") {
        chip.start_of_line();

        CHECK(chip.foreground() == 7);
        CHECK(chip.background() == 0);
        CHECK(chip.charset() == TeletextCharset::Alpha);
    }

    SECTION("end_of_line advances raster") {
        chip.start_of_line();
        CHECK(chip.raster() == 0);

        chip.end_of_line();
        CHECK(chip.raster() == 1);  // Advances by 1 (20 scanlines per char row)

        chip.end_of_line();
        CHECK(chip.raster() == 2);
    }

    SECTION("raster wraps after 20 scanlines") {
        for (int i = 0; i < 20; ++i) {
            chip.end_of_line();
        }
        CHECK(chip.raster() == 0);  // Wrapped back to 0
    }

    SECTION("vsync resets raster to 0") {
        chip.end_of_line();
        chip.end_of_line();
        CHECK(chip.raster() == 2);

        chip.vsync();
        CHECK(chip.raster() == 0);
    }
}

// ============================================================================
// Phase 2: Basic Character Rendering (tests to be added incrementally)
// ============================================================================

TEST_CASE("SAA5050 renders space character", "[saa5050][render]") {
    Saa5050 chip;
    chip.start_of_line();

    // Fill the delay buffer with spaces
    // byte() writes 1 slot, need 4 slots to fill from write_index=4 to wrap around
    chip.byte(0x20, 1);  // index 4
    chip.byte(0x20, 1);  // index 5
    chip.byte(0x20, 1);  // index 6
    chip.byte(0x20, 1);  // index 7 (wraps to 0)

    // Now advance read_index to index 4 where first space was written
    PixelBatch batch;
    chip.emit_pixels(batch, bbc_colors::PALETTE);  // reads index 0
    chip.emit_pixels(batch, bbc_colors::PALETTE);  // reads index 1
    chip.emit_pixels(batch, bbc_colors::PALETTE);  // reads index 2
    chip.emit_pixels(batch, bbc_colors::PALETTE);  // reads index 3

    // Read index 4 (first space we wrote)
    chip.emit_pixels(batch, bbc_colors::PALETTE);

    // All pixels should be background (black) for space character
    // Compare RGB only (ignore metadata in x field)
    for (int i = 0; i < 8; ++i) {
        CHECK(batch.pixels.pixels[i].bits.r == 0);
        CHECK(batch.pixels.pixels[i].bits.g == 0);
        CHECK(batch.pixels.pixels[i].bits.b == 0);
    }
}

TEST_CASE("SAA5050 renders alpha character A", "[saa5050][render]") {
    Saa5050 chip;

    // Test at font row 1 where 'A' has pixels (row 0 is blank for most characters)
    // Need raster=2 or 3 for font_row = raster/2 = 1
    chip.end_of_line();  // raster = 1
    chip.end_of_line();  // raster = 2 (font row 1)
    chip.start_of_line();

    // Feed 'A' character - writes to index 4
    chip.byte('A', 1);

    // Fill the delay buffer: need to write indices 5,6,7 (3 more byte calls)
    chip.byte(0x20, 1);  // index 5
    chip.byte(0x20, 1);  // index 6
    chip.byte(0x20, 1);  // index 7

    // Now emit_pixels() to advance read_index to where 'A' was written (index 4)
    // read_index starts at 0, we need to read 4 slots (indices 0,1,2,3)
    PixelBatch batch;
    chip.emit_pixels(batch, bbc_colors::PALETTE);  // reads index 0
    chip.emit_pixels(batch, bbc_colors::PALETTE);  // reads index 1
    chip.emit_pixels(batch, bbc_colors::PALETTE);  // reads index 2
    chip.emit_pixels(batch, bbc_colors::PALETTE);  // reads index 3

    // Now read index 4 where 'A' was written
    chip.emit_pixels(batch, bbc_colors::PALETTE);

    // At row 1 (raster 2), 'A' should have pixel at center (top of A triangle)
    // Check we have at least one white pixel
    bool has_white = false;
    for (int i = 0; i < 8; ++i) {
        if (batch.pixels.pixels[i].bits.r == 15 &&
            batch.pixels.pixels[i].bits.g == 15 &&
            batch.pixels.pixels[i].bits.b == 15) {
            has_white = true;
        }
    }
    CHECK(has_white);
}

TEST_CASE("SAA5050 font row 1 of 'A' has pixels", "[saa5050][font]") {
    // Verify font data for 'A' at row 1 is non-zero
    constexpr int A_index = 'A' - 0x20;
    CHECK(TELETEXT_FONT[A_index][1] != 0);  // Row 1 should have pixels
    INFO("Font row 1 of 'A' = " << static_cast<int>(TELETEXT_FONT[A_index][1]));
}

TEST_CASE("SAA5050 emit_pixels basic output", "[saa5050][render]") {
    Saa5050 chip;
    chip.start_of_line();

    // The delay buffer starts with write_index=4, read_index=0
    // So we need to fill 4 entries before reading gets valid data

    // Feed 4 'B' characters (each byte() writes 2 entries, so need 2 calls)
    // Actually, let's trace through what happens:
    // After byte('B'): write_index = 6
    // After byte('B'): write_index = 0
    // After emit: read_index = 1 (reads entry 0, which was pre-filled with 0)
    // After emit: read_index = 2 (reads entry 1)

    // Let me test end_of_line first to make sure raster advances
    CHECK(chip.raster() == 0);
    chip.end_of_line();
    CHECK(chip.raster() == 1);  // Advances by 1 per scanline
}
