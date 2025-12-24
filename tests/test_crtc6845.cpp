// test_crtc6845.cpp
// Behaviour tests for MC6845 CRTC
//
// Tests the externally observable output signals:
// - HSYNC/VSYNC timing relative to register settings
// - Display enable signal boundaries
// - Cursor output for different configurations
// - Address counter progression
//
// NOT tested: internal register values (implementation detail)

#include <beebium/devices/Crtc6845.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace beebium;

// Helper to set up common Mode 7 timings
// R0=63 (H total), R1=40 (H displayed), R2=49 (HSYNC pos)
// R3=0x24 (HSYNC width=4, VSYNC width=2), R4=30 (V total), R6=25 (V displayed)
// R7=27 (VSYNC pos), R9=18 (max scanline)
static void setup_mode7_timing(Crtc6845& crtc) {
    // Select and write each register
    crtc.write(0, 0);  crtc.write(1, 63);   // R0: H total = 63 (64 chars)
    crtc.write(0, 1);  crtc.write(1, 40);   // R1: H displayed = 40 chars
    crtc.write(0, 2);  crtc.write(1, 49);   // R2: HSYNC position = 49
    crtc.write(0, 3);  crtc.write(1, 0x24); // R3: HSYNC=4, VSYNC=2
    crtc.write(0, 4);  crtc.write(1, 30);   // R4: V total = 30 (31 rows)
    crtc.write(0, 5);  crtc.write(1, 0);    // R5: V adjust = 0
    crtc.write(0, 6);  crtc.write(1, 25);   // R6: V displayed = 25 rows
    crtc.write(0, 7);  crtc.write(1, 27);   // R7: VSYNC position = 27
    crtc.write(0, 9);  crtc.write(1, 18);   // R9: max scanline = 18 (19 lines/char)
}

// Helper to set up minimal timing for focused tests
// R0=7 (8 char line), R1=4 (4 displayed), R4=3 (4 rows), R6=2 (2 displayed)
static void setup_minimal_timing(Crtc6845& crtc) {
    crtc.write(0, 0);  crtc.write(1, 7);    // R0: H total = 7 (8 chars)
    crtc.write(0, 1);  crtc.write(1, 4);    // R1: H displayed = 4 chars
    crtc.write(0, 2);  crtc.write(1, 5);    // R2: HSYNC position = 5
    crtc.write(0, 3);  crtc.write(1, 0x12); // R3: HSYNC=2, VSYNC=1
    crtc.write(0, 4);  crtc.write(1, 3);    // R4: V total = 3 (4 rows)
    crtc.write(0, 5);  crtc.write(1, 0);    // R5: V adjust = 0
    crtc.write(0, 6);  crtc.write(1, 2);    // R6: V displayed = 2 rows
    crtc.write(0, 7);  crtc.write(1, 3);    // R7: VSYNC position = 3
    crtc.write(0, 9);  crtc.write(1, 1);    // R9: max scanline = 1 (2 lines/char)
}

TEST_CASE("Crtc6845 horizontal display enable", "[crtc6845][display]") {
    Crtc6845 crtc;
    crtc.reset();
    setup_minimal_timing(crtc);

    SECTION("display is active for R1 characters from start of line") {
        // First 4 characters should have display=1
        for (int i = 0; i < 4; ++i) {
            auto out = crtc.tick();
            REQUIRE(out.display == 1);
        }
        // Characters 4-7 should have display=0
        for (int i = 4; i < 8; ++i) {
            auto out = crtc.tick();
            REQUIRE(out.display == 0);
        }
    }

    SECTION("display re-enables at start of next line") {
        // Complete one line (8 chars)
        for (int i = 0; i < 8; ++i) {
            crtc.tick();
        }
        // Next line starts with display=1
        auto out = crtc.tick();
        REQUIRE(out.display == 1);
    }
}

TEST_CASE("Crtc6845 horizontal sync timing", "[crtc6845][hsync]") {
    Crtc6845 crtc;
    crtc.reset();
    setup_minimal_timing(crtc);

    SECTION("HSYNC starts at position R2") {
        // Tick until position 4 (just before HSYNC at 5)
        for (int i = 0; i < 5; ++i) {
            auto out = crtc.tick();
            REQUIRE(out.hsync == 0);
        }
        // Position 5 should start HSYNC
        auto out = crtc.tick();
        REQUIRE(out.hsync == 1);
    }

    SECTION("HSYNC lasts for R3 low nibble characters") {
        // Advance to HSYNC start
        for (int i = 0; i < 5; ++i) {
            crtc.tick();
        }
        // HSYNC width is 2 (from R3 low nibble)
        auto out1 = crtc.tick();
        REQUIRE(out1.hsync == 1);
        auto out2 = crtc.tick();
        REQUIRE(out2.hsync == 1);
        // After width, HSYNC should end
        auto out3 = crtc.tick();
        REQUIRE(out3.hsync == 0);
    }

    SECTION("HSYNC repeats each line") {
        // Complete first line plus start of second
        for (int i = 0; i < 8 + 5; ++i) {
            crtc.tick();
        }
        // HSYNC on second line at position 5
        auto out = crtc.tick();
        REQUIRE(out.hsync == 1);
    }
}

TEST_CASE("Crtc6845 vertical display enable", "[crtc6845][display]") {
    Crtc6845 crtc;
    crtc.reset();
    setup_minimal_timing(crtc);

    // Calculate scanlines per row (R9 + 1 = 2)
    const int scanlines_per_row = 2;
    const int chars_per_line = 8;  // R0 + 1
    const int chars_per_scanline = chars_per_line;

    SECTION("display is active for R6 rows") {
        // Row 0, 1 (R6=2 displayed) should have display active
        for (int row = 0; row < 2; ++row) {
            for (int scanline = 0; scanline < scanlines_per_row; ++scanline) {
                auto out = crtc.tick();  // First char of each scanline
                REQUIRE(out.display == 1);
                // Complete the rest of the scanline
                for (int c = 1; c < chars_per_scanline; ++c) {
                    crtc.tick();
                }
            }
        }
    }

    SECTION("display disables after R6 rows") {
        // Skip first 2 rows (displayed)
        for (int i = 0; i < 2 * scanlines_per_row * chars_per_scanline; ++i) {
            crtc.tick();
        }
        // Row 2 should have display=0 (first visible char position)
        auto out = crtc.tick();
        REQUIRE(out.display == 0);
    }
}

TEST_CASE("Crtc6845 vertical sync timing", "[crtc6845][vsync]") {
    Crtc6845 crtc;
    crtc.reset();
    setup_minimal_timing(crtc);

    const int scanlines_per_row = 2;  // R9 + 1
    const int chars_per_line = 8;     // R0 + 1

    SECTION("VSYNC starts at row R7") {
        // R7 = 3, so VSYNC starts at row 3
        // Skip rows 0, 1, 2
        for (int row = 0; row < 3; ++row) {
            for (int i = 0; i < scanlines_per_row * chars_per_line; ++i) {
                auto out = crtc.tick();
                REQUIRE(out.vsync == 0);
            }
        }
        // Row 3 should have VSYNC
        auto out = crtc.tick();
        REQUIRE(out.vsync == 1);
    }

    SECTION("VSYNC lasts for R3 high nibble scanlines") {
        // Skip to row 3
        for (int i = 0; i < 3 * scanlines_per_row * chars_per_line; ++i) {
            crtc.tick();
        }

        // VSYNC width is 1 scanline (R3 high nibble)
        // First scanline of row 3 should have VSYNC
        for (int i = 0; i < chars_per_line; ++i) {
            auto out = crtc.tick();
            REQUIRE(out.vsync == 1);
        }

        // Second scanline should not have VSYNC (width=1)
        auto out = crtc.tick();
        REQUIRE(out.vsync == 0);
    }
}

TEST_CASE("Crtc6845 address counter", "[crtc6845][address]") {
    Crtc6845 crtc;
    crtc.reset();
    setup_minimal_timing(crtc);

    SECTION("address increments each character") {
        auto out0 = crtc.tick();
        auto out1 = crtc.tick();
        auto out2 = crtc.tick();

        REQUIRE(out1.address == out0.address + 1);
        REQUIRE(out2.address == out1.address + 1);
    }

    SECTION("address resets to line start at end of scanline") {
        // Tick through one scanline (8 chars)
        uint16_t first_addr = crtc.tick().address;
        for (int i = 1; i < 8; ++i) {
            crtc.tick();
        }
        // Start of second scanline (same row) resets to line start
        auto out = crtc.tick();
        REQUIRE(out.address == first_addr);
    }

    SECTION("address advances at end of row") {
        const int scanlines_per_row = 2;
        const int chars_per_line = 8;
        const int displayed_chars = 4;  // R1

        uint16_t first_addr = crtc.tick().address;

        // Complete one row
        for (int i = 1; i < scanlines_per_row * chars_per_line; ++i) {
            crtc.tick();
        }

        // Second row starts at first_addr + displayed_chars (R1)
        auto out = crtc.tick();
        REQUIRE(out.address == first_addr + displayed_chars);
    }

    SECTION("address resets to screen start at end of frame") {
        const int scanlines_per_row = 2;
        const int chars_per_line = 8;
        const int total_rows = 4;  // R4 + 1

        // Set screen start address
        crtc.write(0, 12); crtc.write(1, 0x10);  // R12: high
        crtc.write(0, 13); crtc.write(1, 0x00);  // R13: low = 0x1000

        // Reset to apply start address
        crtc.reset();
        setup_minimal_timing(crtc);
        crtc.write(0, 12); crtc.write(1, 0x10);
        crtc.write(0, 13); crtc.write(1, 0x00);

        // Complete one full frame
        for (int row = 0; row < total_rows; ++row) {
            for (int i = 0; i < scanlines_per_row * chars_per_line; ++i) {
                crtc.tick();
            }
        }

        // New frame starts at screen start address (0x1000)
        auto out = crtc.tick();
        REQUIRE(out.address == 0x1000);
    }
}

TEST_CASE("Crtc6845 cursor output", "[crtc6845][cursor]") {
    Crtc6845 crtc;
    crtc.reset();
    setup_minimal_timing(crtc);

    SECTION("cursor active at cursor position within scanline range") {
        // Set cursor position to address 2
        crtc.write(0, 14); crtc.write(1, 0);   // R14: cursor high
        crtc.write(0, 15); crtc.write(1, 2);   // R15: cursor low = 2
        // Set cursor start=0, end=1, mode=0 (steady)
        crtc.write(0, 10); crtc.write(1, 0);   // R10: start=0, mode=0
        crtc.write(0, 11); crtc.write(1, 1);   // R11: end=1

        // Tick to position 2 (address 0, 1, then 2)
        crtc.tick();  // addr 0
        crtc.tick();  // addr 1
        auto out = crtc.tick();  // addr 2

        REQUIRE(out.cursor == 1);
    }

    SECTION("cursor not active outside scanline range") {
        // Set cursor position to address 0
        crtc.write(0, 14); crtc.write(1, 0);
        crtc.write(0, 15); crtc.write(1, 0);
        // Set cursor start=1, end=1 (only on raster 1)
        crtc.write(0, 10); crtc.write(1, 1);   // start=1, mode=0
        crtc.write(0, 11); crtc.write(1, 1);   // end=1

        // First tick is at raster 0, should NOT show cursor
        auto out = crtc.tick();
        REQUIRE(out.cursor == 0);
    }

    SECTION("cursor mode 1 disables cursor") {
        // Set cursor position to address 0
        crtc.write(0, 14); crtc.write(1, 0);
        crtc.write(0, 15); crtc.write(1, 0);
        // Mode 1 = no cursor (bit 5 set)
        crtc.write(0, 10); crtc.write(1, 0x20);  // start=0, mode=1
        crtc.write(0, 11); crtc.write(1, 1);

        auto out = crtc.tick();
        REQUIRE(out.cursor == 0);
    }

    SECTION("cursor only active in display area") {
        // Set cursor position beyond display (address 5, but display ends at 4)
        crtc.write(0, 14); crtc.write(1, 0);
        crtc.write(0, 15); crtc.write(1, 5);   // cursor at address 5
        crtc.write(0, 10); crtc.write(1, 0);   // mode=0 steady
        crtc.write(0, 11); crtc.write(1, 1);

        // Skip to position 5 (outside display)
        for (int i = 0; i < 5; ++i) {
            crtc.tick();
        }
        auto out = crtc.tick();
        // Cursor should NOT be active (display=0 at this position)
        REQUIRE(out.cursor == 0);
    }
}

TEST_CASE("Crtc6845 raster counter", "[crtc6845][raster]") {
    Crtc6845 crtc;
    crtc.reset();
    setup_minimal_timing(crtc);

    const int chars_per_line = 8;

    SECTION("raster increments each scanline within row") {
        auto out0 = crtc.tick();
        REQUIRE(out0.raster == 0);

        // Complete first scanline
        for (int i = 1; i < chars_per_line; ++i) {
            crtc.tick();
        }

        // Second scanline, raster = 1
        auto out1 = crtc.tick();
        REQUIRE(out1.raster == 1);
    }

    SECTION("raster resets at start of new row") {
        const int scanlines_per_row = 2;

        // Complete one row
        for (int i = 0; i < scanlines_per_row * chars_per_line; ++i) {
            crtc.tick();
        }

        // New row starts at raster 0
        auto out = crtc.tick();
        REQUIRE(out.raster == 0);
    }
}

TEST_CASE("Crtc6845 register access", "[crtc6845][registers]") {
    Crtc6845 crtc;
    crtc.reset();

    SECTION("registers 12-17 are readable") {
        // Set screen start
        crtc.write(0, 12); crtc.write(1, 0x15);
        crtc.write(0, 13); crtc.write(1, 0xAB);

        // Read back
        crtc.write(0, 12);
        REQUIRE(crtc.read(1) == 0x15);
        crtc.write(0, 13);
        REQUIRE(crtc.read(1) == 0xAB);
    }

    SECTION("registers 0-11 return 0 on read") {
        crtc.write(0, 0); crtc.write(1, 0xFF);

        crtc.write(0, 0);
        REQUIRE(crtc.read(1) == 0);
    }

    SECTION("register values are masked") {
        // R12 is 6 bits
        crtc.write(0, 12); crtc.write(1, 0xFF);
        crtc.write(0, 12);
        REQUIRE(crtc.read(1) == 0x3F);  // Masked to 6 bits
    }
}

TEST_CASE("Crtc6845 reset", "[crtc6845][reset]") {
    Crtc6845 crtc;

    SECTION("reset clears position counters") {
        setup_minimal_timing(crtc);

        // Advance some state
        for (int i = 0; i < 100; ++i) {
            crtc.tick();
        }

        crtc.reset();
        setup_minimal_timing(crtc);

        // First tick after reset with valid timing
        auto out = crtc.tick();
        REQUIRE(out.address == 0);
        REQUIRE(out.raster == 0);
        REQUIRE(out.hsync == 0);
        REQUIRE(out.vsync == 0);
    }

    SECTION("reset with valid timing enables display") {
        crtc.reset();
        setup_minimal_timing(crtc);

        auto out = crtc.tick();
        REQUIRE(out.display == 1);  // Display enabled in visible area
    }
}
