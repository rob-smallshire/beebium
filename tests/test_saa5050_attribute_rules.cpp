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

// test_saa5050_attribute_rules.cpp
//
// The SAA5050 attribute rules as the real chip behaves, one vector per rule
// (GitHub issue #175). The rules, their sources and the vector numbering are
// in docs/discussion/saa5050-conformance-study.md, sections 7 and 8.2; the
// behaviour was established against captures from a real BBC Micro.
//
// Each vector is a row (or rows) of screen-memory bytes. The page is driven
// through the chip as the BBC drives it -- ten lines per character row, both
// interlaced fields (CRS low and high) so all twenty glyph lines are drawn --
// and every cell's pixels are decoded back to the chip's twelve dots and the
// palette colour of each. The expectation for a cell is a glyph (or none), a
// foreground and a background, so a failure prints both cells as colour maps.
//
// "Set-At": a code's own cell is displayed with the new state. "Set-After":
// the change takes effect from the next cell. For most codes the difference
// shows only under Hold Graphics, when a control code's cell displays the
// held mosaic instead of a space.

#include <catch2/catch_test_macros.hpp>

#include <beebium/PixelBatch.hpp>
#include <beebium/Saa5050.hpp>
#include <beebium/TeletextFont.hpp>
#include <beebium/TeletextGrid.hpp>

#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace beebium;

namespace {

// Colour indices, as the BBC's 3-bit RGB.
enum Colour : uint8_t { K = 0, R = 1, G = 2, Y = 3, W = 7 };

constexpr int DOTS = 12;
constexpr int GLYPH_LINES = 20;

std::array<VideoDataPixel, 8> make_palette() {
    std::array<VideoDataPixel, 8> palette{};
    for (size_t i = 0; i < palette.size(); ++i) {
        VideoDataPixel pixel;
        pixel.bits.r = static_cast<uint16_t>(i & 1 ? 15 : 0);
        pixel.bits.g = static_cast<uint16_t>(i & 2 ? 15 : 0);
        pixel.bits.b = static_cast<uint16_t>(i & 4 ? 15 : 0);
        pixel.bits.x = 0;
        palette[i] = pixel;
    }
    return palette;
}

const std::array<VideoDataPixel, 8> PALETTE = make_palette();

bool same_colour(VideoDataPixel a, VideoDataPixel b) {
    return a.bits.r == b.bits.r && a.bits.g == b.bits.g && a.bits.b == b.bits.b;
}

uint8_t pure_index(VideoDataPixel pixel) {
    for (uint8_t i = 0; i < 8; ++i) {
        if (same_colour(pixel, PALETTE[i])) return i;
    }
    throw std::runtime_error("pixel is not a palette colour");
}

VideoDataPixel blend(VideoDataPixel a, VideoDataPixel b) {
    VideoDataPixel result;
    result.bits.r = TELETEXT_BLEND_TABLE[a.bits.r][b.bits.r];
    result.bits.g = TELETEXT_BLEND_TABLE[a.bits.g][b.bits.g];
    result.bits.b = TELETEXT_BLEND_TABLE[a.bits.b][b.bits.b];
    result.bits.x = 0;
    return result;
}

// The palette colour b such that blend(a, b) and blend(c, b) give the two
// blended pixels the chip emitted between pure dots a and c.
uint8_t blended_index(uint8_t a, uint8_t c, VideoDataPixel ab, VideoDataPixel cb) {
    for (uint8_t i = 0; i < 8; ++i) {
        if (same_colour(blend(PALETTE[a], PALETTE[i]), ab) &&
            same_colour(blend(PALETTE[c], PALETTE[i]), cb)) {
            return i;
        }
    }
    throw std::runtime_error("blended pixels do not decode to a palette colour");
}

// A cell as displayed: the colour index of each of its 12 dots on each of its
// 20 glyph lines.
struct Cell {
    std::array<std::array<uint8_t, DOTS>, GLYPH_LINES> dots{};

    bool operator==(const Cell&) const = default;
};

std::string to_string(const Cell& cell) {
    static const char* NAMES = "KRGYBMCW";
    std::string text;
    for (const auto& line : cell.dots) {
        text += "\n  ";
        for (uint8_t dot : line) text += NAMES[dot];
    }
    return text;
}

// Decode the 8 + 8 pixels of one cell's line back to the chip's 12 dots. Dots
// 0, 2, 3 and 5 of each half are emitted pure; dots 1 and 4 only inside the
// blends either side of them.
std::array<uint8_t, DOTS> decode_line(const PixelBatch& left, const PixelBatch& right) {
    std::array<uint8_t, DOTS> dots{};
    const PixelBatch* halves[2] = {&left, &right};
    for (int h = 0; h < 2; ++h) {
        const auto& px = halves[h]->pixels.pixels;
        uint8_t* d = &dots[static_cast<size_t>(h * 6)];
        d[0] = pure_index(px[0]);
        d[2] = pure_index(px[3]);
        d[3] = pure_index(px[4]);
        d[5] = pure_index(px[7]);
        d[1] = blended_index(d[0], d[2], px[1], px[2]);
        d[4] = blended_index(d[3], d[5], px[5], px[6]);
    }
    return dots;
}

enum class Flash { Visible, Hidden };

using Row = std::vector<uint8_t>;
using Page = std::vector<std::vector<Cell>>;

// Drive rows of screen bytes through a fresh chip and return every cell.
//
// The flash phase is chosen by stepping the chip's field count: fields 0-15
// of its 64-field cycle hide flashing text, 16-63 show it. Both fields of the
// frame are drawn in the same phase.
Page render(const std::vector<Row>& rows, Flash flash = Flash::Visible,
            TeletextGrid* grid = nullptr) {
    Saa5050 chip;
    const int fields = flash == Flash::Hidden ? 1 : 16;
    for (int i = 0; i < fields; ++i) chip.vsync();
    chip.set_teletext_grid(grid);

    Page page(rows.size());
    for (size_t r = 0; r < rows.size(); ++r) page[r].resize(rows[r].size());

    for (int crs = 0; crs < 2; ++crs) {
        chip.end_of_vsync();
        for (size_t r = 0; r < rows.size(); ++r) {
            for (int line = 0; line < Saa5050::ROW_LINES; ++line) {
                chip.start_of_line();
                chip.set_crs(crs != 0);
                for (size_t c = 0; c < rows[r].size(); ++c) {
                    chip.byte(rows[r][c], 1);
                    PixelBatch left, right;
                    chip.emit_pixels(left, PALETTE.data());
                    chip.emit_pixels(right, PALETTE.data());
                    page[r][c].dots[static_cast<size_t>(2 * line + crs)] = decode_line(left, right);
                }
                chip.end_of_line();
            }
        }
        if (grid && crs == 0) {
            // Publish the first field's capture; both fields capture alike.
            grid->swap();
        }
    }
    return page;
}

std::vector<Cell> render_row(const Row& row, Flash flash = Flash::Visible) {
    return render({row}, flash)[0];
}

// What a cell should show.
enum class Height { Normal, DoubleTop, DoubleBottom };

struct Glyph {
    TeletextCharset charset;
    uint8_t character;  // 0x20-0x7F
    Height height = Height::Normal;
};

Cell expect(std::optional<Glyph> glyph, uint8_t fg, uint8_t bg) {
    Cell cell;
    for (int line = 0; line < GLYPH_LINES; ++line) {
        uint16_t bits = 0;
        if (glyph) {
            int font_line = line;
            if (glyph->height == Height::DoubleTop) font_line = line >> 1;
            if (glyph->height == Height::DoubleBottom) font_line = (line + 20) >> 1;
            if (font_line < GLYPH_LINES) {
                bits = TELETEXT_EXPANDED_FONT[1][static_cast<int>(glyph->charset)]
                                             [glyph->character - 32][font_line];
            }
        }
        for (int dot = 0; dot < DOTS; ++dot) {
            cell.dots[static_cast<size_t>(line)][static_cast<size_t>(dot)] =
                (bits >> dot) & 1 ? fg : bg;
        }
    }
    return cell;
}

// Shorthands for the cells the vectors use.
Cell blank(uint8_t bg = K) { return expect(std::nullopt, W, bg); }
Cell block(uint8_t fg, uint8_t bg = K, Height height = Height::Normal) {
    return expect(Glyph{TeletextCharset::ContiguousGraphics, 0x7F, height}, fg, bg);
}
Cell separated_block(uint8_t fg, uint8_t bg = K) {
    return expect(Glyph{TeletextCharset::SeparatedGraphics, 0x7F}, fg, bg);
}
Cell text(char ch, uint8_t fg, uint8_t bg = K, Height height = Height::Normal) {
    return expect(Glyph{TeletextCharset::Alpha, static_cast<uint8_t>(ch), height}, fg, bg);
}

void check_row(const std::vector<Cell>& got, const std::vector<Cell>& want) {
    REQUIRE(got.size() == want.size());
    for (size_t c = 0; c < got.size(); ++c) {
        INFO("cell " << c << "\n got:" << to_string(got[c]) << "\nwant:" << to_string(want[c]));
        CHECK(got[c] == want[c]);
    }
}

// Screen-memory bytes: control codes with bit 7 set, as MODE 7 stores them.
enum Code : uint8_t {
    NUL = 0x80, ALPHA_RED = 0x81, ALPHA_GREEN = 0x82, ALPHA_YELLOW = 0x83,
    ALPHA_MAGENTA = 0x85, ALPHA_WHITE = 0x87,
    FLASH = 0x88, STEADY = 0x89, END_BOX = 0x8A, START_BOX = 0x8B,
    NORMAL_HEIGHT = 0x8C, DOUBLE_HEIGHT = 0x8D, SHIFT_OUT = 0x8E, SHIFT_IN = 0x8F,
    DLE = 0x90, GRAPHICS_RED = 0x91, GRAPHICS_YELLOW = 0x93, GRAPHICS_WHITE = 0x97,
    CONCEAL = 0x98, CONTIGUOUS = 0x99, SEPARATED = 0x9A, ESCAPE = 0x9B,
    BLACK_BACKGROUND = 0x9C, NEW_BACKGROUND = 0x9D, HOLD = 0x9E, RELEASE = 0x9F,
    BLOCK = 0xFF,  // every sixel: a solid mosaic, or the alpha block glyph
};

} // namespace

// --- Colour codes (R2, R3; study defect 6.1) -------------------------------

TEST_CASE("V1: a graphics colour code shows the held mosaic in the old colour",
          "[saa5050][attributes][hold]") {
    // The code is Set-After for the foreground: the held block in its cell
    // is still white, and only the block after it is yellow.
    check_row(render_row({GRAPHICS_WHITE, BLOCK, HOLD, GRAPHICS_YELLOW, BLOCK}),
              {blank(), block(W), block(W), block(W), block(Y)});
}

TEST_CASE("V2: an alpha colour code shows the held mosaic in the old colour",
          "[saa5050][attributes][hold]") {
    check_row(render_row({GRAPHICS_WHITE, BLOCK, HOLD, ALPHA_YELLOW, 'A' | 0x80}),
              {blank(), block(W), block(W), block(W), text('A', Y)});
}

TEST_CASE("V3: an alpha colour code clears the held mosaic after its own cell",
          "[saa5050][attributes][hold]") {
    check_row(render_row({GRAPHICS_WHITE, BLOCK, HOLD, ALPHA_MAGENTA, BLACK_BACKGROUND}),
              {blank(), block(W), block(W), block(W), blank()});
}

// --- Flash and steady (R4, R5, R7; study defects 6.2 and 6.3) --------------

TEST_CASE("V4: a held flashing mosaic is remembered whole and steadied at the STEADY",
          "[saa5050][attributes][hold][flash]") {
    // Two spaces, two flashing blocks, two steady blocks. The held memory
    // keeps the mosaic's bitmap whatever the flash phase, and STEADY is
    // Set-At, so its cell shows the held block steadily.
    const Row row = {GRAPHICS_WHITE, FLASH, BLOCK, HOLD, STEADY, BLOCK};

    SECTION("flash visible") {
        check_row(render_row(row, Flash::Visible),
                  {blank(), blank(), block(W), block(W), block(W), block(W)});
    }
    SECTION("flash hidden") {
        check_row(render_row(row, Flash::Hidden),
                  {blank(), blank(), blank(), blank(), block(W), block(W)});
    }
}

TEST_CASE("V5: a held mosaic flashes in control-code cells after FLASH",
          "[saa5050][attributes][hold][flash]") {
    // FLASH is Set-After: its own cell shows the held block steadily, and
    // the held block in the cell after it flashes.
    const Row row = {GRAPHICS_WHITE, BLOCK, HOLD, FLASH, BLACK_BACKGROUND};

    SECTION("flash visible") {
        check_row(render_row(row, Flash::Visible),
                  {blank(), block(W), block(W), block(W), block(W)});
    }
    SECTION("flash hidden") {
        check_row(render_row(row, Flash::Hidden),
                  {blank(), block(W), block(W), block(W), blank()});
    }
}

// --- Conceal (R8, R9, R10; study defect 6.4) -------------------------------

TEST_CASE("V6: CONCEAL hides the held mosaic in its own cell",
          "[saa5050][attributes][hold][conceal]") {
    SECTION("concealed") {
        // CONCEAL is Set-At. The colour code that ends it is Set-After, so
        // its own cell is still concealed; the held memory survives.
        check_row(render_row({GRAPHICS_WHITE, BLOCK, HOLD, CONCEAL, BLACK_BACKGROUND,
                              GRAPHICS_WHITE, BLACK_BACKGROUND}),
                  {blank(), block(W), block(W), blank(), blank(), blank(), block(W)});
    }
    SECTION("revealed") {
        // The BBC has no reveal in hardware: software rewrites CONCEAL as
        // ESC, which the chip ignores.
        check_row(render_row({GRAPHICS_WHITE, BLOCK, HOLD, ESCAPE, BLACK_BACKGROUND,
                              GRAPHICS_WHITE, BLACK_BACKGROUND}),
                  {blank(), block(W), block(W), block(W), block(W), block(W), block(W)});
    }
}

// --- What the held memory holds and what clears it (R14, R15, R16) ---------

TEST_CASE("V7: a control code displayed while hold is off clears the held memory",
          "[saa5050][attributes][hold]") {
    // The SAA5050's own behaviour, unlike a teletext TV's: NEW BACKGROUND
    // displayed without hold forgets the block, so HOLD has nothing to show.
    check_row(render_row({GRAPHICS_WHITE, BLOCK, NEW_BACKGROUND, HOLD, BLACK_BACKGROUND}),
              {blank(), block(W), blank(W), blank(W), blank()});
}

TEST_CASE("V8: an alphanumeric in graphics mode does not clear the held memory",
          "[saa5050][attributes][hold]") {
    check_row(render_row({GRAPHICS_WHITE, BLOCK, 'A' | 0x80, HOLD, BLACK_BACKGROUND}),
              {blank(), block(W), text('A', W), block(W), block(W)});
}

TEST_CASE("V9: a change of height blanks its own cell and clears the held memory",
          "[saa5050][attributes][hold][height]") {
    SECTION("double height, a change") {
        check_row(render({{GRAPHICS_WHITE, BLOCK, HOLD, DOUBLE_HEIGHT, BLACK_BACKGROUND},
                          {}})[0],
                  {blank(), block(W), block(W), blank(), blank()});
    }
    SECTION("normal height, no change") {
        check_row(render_row({GRAPHICS_WHITE, BLOCK, HOLD, NORMAL_HEIGHT, BLACK_BACKGROUND}),
                  {blank(), block(W), block(W), block(W), block(W)});
    }
    SECTION("normal height after double height, a change") {
        check_row(render({{GRAPHICS_WHITE, DOUBLE_HEIGHT, BLOCK, HOLD, NORMAL_HEIGHT,
                           BLACK_BACKGROUND},
                          {}})[0],
                  {blank(), blank(), block(W, K, Height::DoubleTop),
                   block(W, K, Height::DoubleTop), blank(), blank()});
    }
}

TEST_CASE("V10: a held mosaic keeps the separation it was drawn with",
          "[saa5050][attributes][hold]") {
    check_row(render_row({GRAPHICS_WHITE, SEPARATED, BLOCK, HOLD, CONTIGUOUS, BLACK_BACKGROUND}),
              {blank(), blank(), separated_block(W), separated_block(W), separated_block(W),
               separated_block(W)});
}

// --- Backgrounds (R11) -----------------------------------------------------

TEST_CASE("V11: background codes are Set-At and NEW BACKGROUND copies the foreground",
          "[saa5050][attributes][background]") {
    check_row(render_row({ALPHA_RED, NEW_BACKGROUND, ALPHA_WHITE, 'A' | 0x80, BLACK_BACKGROUND,
                          'A' | 0x80}),
              {blank(), blank(R), blank(R), text('A', W, R), blank(), text('A', W)});
}

// --- Double height (R19) ---------------------------------------------------

TEST_CASE("V12: the lower row of double height is read from its own memory",
          "[saa5050][attributes][height]") {
    // On the BBC the row below a double-height row is not a copy: its own
    // codes and characters supply the lower halves.
    const Page page = render({{DOUBLE_HEIGHT, 'A' | 0x80}, {ALPHA_GREEN, DOUBLE_HEIGHT, 'B' | 0x80}});
    check_row(page[0], {blank(), text('A', W, K, Height::DoubleTop)});
    check_row(page[1], {blank(), blank(), text('B', G, K, Height::DoubleBottom)});
}

TEST_CASE("V13: normal-height text on the lower row of double height is blank",
          "[saa5050][attributes][height]") {
    const Row row = {DOUBLE_HEIGHT, 'A' | 0x80, NORMAL_HEIGHT, 'C' | 0x80};
    const Page page = render({row, row});
    check_row(page[0], {blank(), text('A', W, K, Height::DoubleTop), blank(), text('C', W)});
    check_row(page[1], {blank(), text('A', W, K, Height::DoubleBottom), blank(), blank()});
}

// --- Hold and release (R12, R13) -------------------------------------------

TEST_CASE("V14: HOLD is Set-At and RELEASE is Set-After",
          "[saa5050][attributes][hold]") {
    check_row(render_row({GRAPHICS_WHITE, BLOCK, HOLD, RELEASE, BLACK_BACKGROUND}),
              {blank(), block(W), block(W), block(W), blank()});
}

// --- Flash timing (R6) -----------------------------------------------------

TEST_CASE("V15: flashing text is hidden for 16 fields of every 64",
          "[saa5050][attributes][flash]") {
    // Not verifiable from still captures; this pins the cycle the chip is
    // built with. Fields are counted at VSYNC from reset.
    for (int fields : {1, 15, 16, 40, 63, 64, 65, 79, 80}) {
        Saa5050 chip;
        for (int i = 0; i < fields; ++i) chip.vsync();
        chip.end_of_vsync();
        chip.start_of_line();
        chip.byte(GRAPHICS_WHITE, 1);
        chip.byte(FLASH, 1);
        const bool hidden = fields % 64 < 16;
        INFO("fields " << fields);
        CHECK(chip.is_flash_enabled() == hidden);
        // The mosaic after FLASH lights its top line only in the visible phase.
        chip.byte(BLOCK, 1);
        PixelBatch discard, left, right;
        for (int i = 0; i < 4; ++i) chip.emit_pixels(discard, PALETTE.data());
        chip.emit_pixels(left, PALETTE.data());
        chip.emit_pixels(right, PALETTE.data());
        CHECK(same_colour(left.pixels.pixels[0], PALETTE[hidden ? K : W]));
    }
}

// --- Codes the chip ignores (R20) ------------------------------------------

TEST_CASE("V17: box, shift, DLE, ESC and NUL change nothing and show the held mosaic",
          "[saa5050][attributes][hold]") {
    check_row(render_row({GRAPHICS_WHITE, BLOCK, HOLD, END_BOX, START_BOX, SHIFT_OUT, SHIFT_IN,
                          DLE, ESCAPE, NUL, BLOCK}),
              {blank(), block(W), block(W), block(W), block(W), block(W), block(W), block(W),
               block(W), block(W), block(W)});
}

// --- Row start (R1) --------------------------------------------------------

TEST_CASE("V18: every row starts alpha white on black, steady, revealed, unheld",
          "[saa5050][attributes]") {
    const std::vector<Row> rows = {
        {GRAPHICS_RED, SEPARATED, HOLD, FLASH, NEW_BACKGROUND, CONCEAL, DOUBLE_HEIGHT, BLOCK},
        {},
        {'A' | 0x80, BLOCK},
    };
    for (Flash flash : {Flash::Visible, Flash::Hidden}) {
        const Page page = render(rows, flash);
        // BLOCK in alpha mode is the alpha block glyph, not a mosaic.
        check_row(page[2], {text('A', W), text(0x7F, W)});
    }
}

// --- What the screen-text grid records -------------------------------------

TEST_CASE("The grid records a held mosaic under a colour code in the old foreground",
          "[saa5050][attributes][hold][grid]") {
    TeletextGrid grid;
    render({{GRAPHICS_WHITE, BLOCK, HOLD, GRAPHICS_YELLOW, BLOCK}}, Flash::Visible, &grid);
    const TeletextCell& held = grid.cell(0, 3);
    CHECK(held.is_control_code);
    CHECK(held.character == 0x7F);
    CHECK(held.fg == W);
    CHECK(grid.cell(0, 4).fg == Y);
}

TEST_CASE("The grid records flashing from the cell after FLASH and steady at STEADY",
          "[saa5050][attributes][hold][grid][flash]") {
    SECTION("FLASH") {
        TeletextGrid grid;
        render({{GRAPHICS_WHITE, BLOCK, HOLD, FLASH, BLACK_BACKGROUND}}, Flash::Hidden, &grid);
        CHECK_FALSE(grid.cell(0, 3).flashing);
        CHECK(grid.cell(0, 4).flashing);
    }
    SECTION("STEADY") {
        TeletextGrid grid;
        render({{GRAPHICS_WHITE, FLASH, BLOCK, HOLD, STEADY, BLOCK}}, Flash::Hidden, &grid);
        CHECK(grid.cell(0, 3).flashing);
        CHECK_FALSE(grid.cell(0, 4).flashing);
    }
}

TEST_CASE("The grid records concealment from CONCEAL to the colour code that ends it",
          "[saa5050][attributes][hold][grid][conceal]") {
    TeletextGrid grid;
    render({{GRAPHICS_WHITE, BLOCK, HOLD, CONCEAL, BLACK_BACKGROUND, GRAPHICS_WHITE,
             BLACK_BACKGROUND}},
           Flash::Visible, &grid);
    CHECK_FALSE(grid.cell(0, 2).concealed);
    CHECK(grid.cell(0, 3).concealed);
    CHECK(grid.cell(0, 4).concealed);
    CHECK(grid.cell(0, 5).concealed);
    CHECK_FALSE(grid.cell(0, 6).concealed);
}
