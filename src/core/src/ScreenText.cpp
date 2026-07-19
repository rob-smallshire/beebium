// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#include <beebium/ScreenText.hpp>

#include <beebium/TeletextText.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace beebium::screen {

namespace {

// A BBC character cell is eight logical pixels wide in every bitmap mode --
// MODE 0 is 640 pixels over 80 columns, MODE 2 is 160 over 20 -- because the
// framebuffer holds logical pixels and the client stretches them.
constexpr uint32_t BITMAP_CELL_WIDTH = 8;

// A glyph is eight scanlines tall whatever the row pitch. MODE 3 and MODE 6
// leave the two spare scanlines of their ten-scanline pitch blank.
constexpr uint32_t BITMAP_CELL_HEIGHT = 8;

// The SAA5050 draws a character as twelve pixels across two batches of six.
constexpr uint32_t TELETEXT_CELL_WIDTH = 12;

void strip_trailing_spaces(std::string& line) {
    while (!line.empty() && line.back() == ' ') {
        line.pop_back();
    }
}

void append_utf8(std::string& out, char32_t codepoint) {
    if (codepoint < 0x80) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

// Which grid cells of a band a pixel rectangle covers.
struct CellRange {
    uint32_t first_row = 0;
    uint32_t last_row = 0;      // One past
    uint32_t first_column = 0;
    uint32_t last_column = 0;   // One past

    [[nodiscard]] bool empty() const {
        return last_row <= first_row || last_column <= first_column;
    }
};

// Cells whose top-left corner falls inside the rectangle, clipped to the grid.
//
// A partly covered cell is taken whole: a drag that clips the right-hand half
// of a character selected that character, and returning half a glyph is not a
// thing text can express.
CellRange cells_covered(const Band& band,
                        const PixelRect& region,
                        uint32_t columns,
                        uint32_t rows) {
    const PixelRect band_rect{band.origin_x, band.top,
                              columns * band.column_pitch,
                              band.bottom - band.top};
    const PixelRect covered = band_rect.intersected(region);
    if (covered.empty() || band.column_pitch == 0 || band.row_pitch == 0) {
        return {};
    }

    CellRange range;
    range.first_column = (covered.x - band.origin_x) / band.column_pitch;
    range.last_column =
        (covered.right() - band.origin_x + band.column_pitch - 1) / band.column_pitch;
    range.first_row = (covered.y - band.origin_y) / band.row_pitch;
    range.last_row =
        (covered.bottom() - band.origin_y + band.row_pitch - 1) / band.row_pitch;

    range.last_column = std::min(range.last_column, columns);
    range.last_row = std::min(range.last_row, rows);
    range.first_column = std::min(range.first_column, range.last_column);
    range.first_row = std::min(range.first_row, range.last_row);
    return range;
}

// Read a band the SAA5050 was driving.
//
// The characters are taken from the grid the chip filled, not recognised in
// pixels: they are known exactly, with their attributes, before any pixels
// exist, and sending them through image recognition would be converting
// information into a picture in order to guess it back. Nothing here can be
// uncertain, so both uncertainty counts stay zero.
//
// What a cell copies as matches teletext_text(): graphics, control codes,
// concealed cells and the bottom half of a double-height row all occupy their
// column as a space, so what is copied lines up with what is displayed.
BandReading read_teletext_band(const Band& band,
                               const PixelRect& region,
                               const TeletextGrid::Snapshot& teletext) {
    BandReading reading;
    if (!teletext.active) {
        // The grid holds whatever was last shown in MODE 7. A band the
        // SAA5050 was not driving must not be read from it.
        return reading;
    }

    reading.supported = true;

    const CellRange range = cells_covered(
        band, region,
        static_cast<uint32_t>(TeletextGrid::COLUMNS),
        static_cast<uint32_t>(TeletextGrid::ROWS));
    if (range.empty()) {
        return reading;
    }

    for (uint32_t row = range.first_row; row < range.last_row; ++row) {
        std::string line;
        for (uint32_t column = range.first_column; column < range.last_column; ++column) {
            const TeletextCell& cell = teletext.cell(row, column);

            const bool readable =
                !cell.is_control_code
                && !cell.concealed
                && !cell.double_height_bottom
                && cell.charset == TeletextCellCharset::Alpha;

            const char32_t codepoint =
                readable ? teletext_alpha_codepoint(cell.character) : 0;

            if (codepoint == 0) {
                line.push_back(' ');
            } else {
                append_utf8(line, codepoint);
            }
        }

        // Measured before trailing spaces are stripped: running right up to
        // the edge is what distinguishes a wrapped line from one that ended.
        const bool reached_the_edge = !line.empty() && line.back() != ' ';
        const uint32_t cells = range.last_column - range.first_column;
        strip_trailing_spaces(line);

        // A run per row of the region, blank rows included. The blank rows are
        // part of what a selection captured, and carrying them is what lets
        // the layouts reproduce the shape of the screen.
        TextRun run;
        run.text = std::move(line);
        run.bounds = {band.origin_x + range.first_column * band.column_pitch,
                      band.origin_y + row * band.row_pitch,
                      cells * band.column_pitch,
                      band.row_pitch};
        run.cell_width = band.cell_width;
        run.cell_height = band.cell_height;
        run.reached_right_edge = reached_the_edge;
        reading.runs.push_back(std::move(run));
    }

    return reading;
}

} // namespace

PixelRect PixelRect::intersected(const PixelRect& other) const {
    const uint32_t left = std::max(x, other.x);
    const uint32_t top = std::max(y, other.y);
    const uint32_t right = std::min(this->right(), other.right());
    const uint32_t bottom = std::min(this->bottom(), other.bottom());

    if (right <= left || bottom <= top) {
        return {};
    }
    return {left, top, right - left, bottom - top};
}

std::vector<Band> bands_of(const FrameMetadata& metadata) {
    std::vector<Band> bands;
    bands.reserve(metadata.regions.size());

    for (const FrameDisplayRegion& region : metadata.regions) {
        Band band;
        band.top = region.start_line;
        band.bottom = region.end_line;
        band.is_teletext = region.is_teletext;

        // The framebuffer's origin is the top-left of the active area -- the
        // renderer resets x and y when display enable goes high -- so the grid
        // starts at the left edge of the frame and the top of the band.
        band.origin_x = 0;
        band.origin_y = region.start_line;

        if (region.is_teletext) {
            // A teletext display is 40 by 25 cells, which is not an assumption
            // about the mode but the shape of the chip: the SAA5050 has no
            // other. Taking the pitch from the band rather than from R9 keeps
            // it right through the interlacing that doubles the frame.
            band.cell_width = TELETEXT_CELL_WIDTH;
            band.column_pitch = TELETEXT_CELL_WIDTH;
            const uint32_t height = region.end_line - region.start_line;
            band.row_pitch = height / static_cast<uint32_t>(TeletextGrid::ROWS);
            // The chip fills the whole cell; there are no blanked scanlines
            // between teletext rows.
            band.cell_height = band.row_pitch;
        } else {
            band.cell_width = BITMAP_CELL_WIDTH;
            band.column_pitch = BITMAP_CELL_WIDTH;
            band.row_pitch = region.char_scanlines;
            // MODE 3 and MODE 6 blank the spare scanlines of a ten-scanline
            // pitch rather than painting them, so the cell is the glyph and
            // the pitch is the step.
            band.cell_height = std::min<uint32_t>(band.row_pitch, BITMAP_CELL_HEIGHT);
        }

        bands.push_back(band);
    }

    return bands;
}

BandReading read_band(const Band& band,
                      const PixelRect& region,
                      Search /*search*/,
                      const TeletextGrid::Snapshot& teletext) {
    // The strategy is chosen from the hardware state that was in effect when
    // these scanlines were drawn, not from anything the caller said. A new
    // strategy is added by extending this dispatch; nothing above it, on the
    // wire or in a client, has to know it happened.
    //
    // The search mode is recorded and passed through to the strategies. The
    // teletext strategy has no use for it -- the grid is the grid -- and the
    // strategies that recognise glyphs in pixels choose between walking the
    // grid and searching sub-cell offsets on the strength of it.
    if (band.is_teletext) {
        return read_teletext_band(band, region, teletext);
    }

    // No strategy reads pixels yet, so the band says so rather than
    // contributing something stale. The counts stay zero: nothing was tried,
    // which is not the same as having tried and failed on a cell.
    return BandReading{};
}

Reading merge(std::vector<BandReading> readings, Layout layout) {
    Reading merged;

    for (BandReading& band : readings) {
        merged.supported = merged.supported || band.supported;
        merged.unreadable_cells += band.unreadable_cells;
        merged.ambiguous_cells += band.ambiguous_cells;

        // Within a band, by baseline then x. Stable, so a strategy that
        // already emits in order keeps the order it chose.
        std::stable_sort(band.runs.begin(), band.runs.end(),
                         [](const TextRun& a, const TextRun& b) {
                             if (a.bounds.y != b.bounds.y) {
                                 return a.bounds.y < b.bounds.y;
                             }
                             return a.bounds.x < b.bounds.x;
                         });

        // Bands arrive top to bottom and do not overlap, and no strategy may
        // produce overlapping runs, so this is concatenation: there is nothing
        // to dedupe and nothing that would need to be.
        merged.runs.insert(merged.runs.end(),
                           std::make_move_iterator(band.runs.begin()),
                           std::make_move_iterator(band.runs.end()));
    }

    merged.text = linearise(merged.runs, layout);
    return merged;
}

std::string linearise(const std::vector<TextRun>& runs, Layout layout) {
    std::string text;
    bool first = true;
    uint32_t previous_baseline = 0;
    bool previous_reached_the_edge = false;

    for (const TextRun& run : runs) {
        if (first) {
            first = false;
        } else if (run.bounds.y == previous_baseline) {
            // Runs sharing a baseline are on the same line. A space keeps two
            // pieces of text found apart on that line from running together.
            text.push_back(' ');
        } else if (layout == Layout::Rows || !previous_reached_the_edge) {
            text.push_back('\n');
        }
        // Flowed and the previous run reached the edge: the line wrapped
        // rather than ended, so it continues with no break at all.

        text += run.text;
        previous_baseline = run.bounds.y;
        previous_reached_the_edge = run.reached_right_edge;
    }

    // A screen is as many rows tall as it is whatever is written on it, so a
    // copy otherwise ends in the blank rows below the text.
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) {
        text.pop_back();
    }

    return text;
}

} // namespace beebium::screen
