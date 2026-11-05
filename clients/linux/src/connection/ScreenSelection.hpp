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

#ifndef BEEBIUM_LINUX_CONNECTION_SCREEN_SELECTION_HPP
#define BEEBIUM_LINUX_CONNECTION_SCREEN_SELECTION_HPP

#include "connection/ScreenReading.hpp"

#include <string>
#include <vector>

namespace beebium::linux_client {

// Screen text selection as geometry (client specification 6.5), in frame
// pixels, the space GetScreenText's regions and runs share: what a drag
// selects in each interpretation, what is lit, and what a copy takes. The
// server reads the text; nothing here recognises anything.

// A point in frame pixels, unsnapped: where a drag began or now is.
struct FramePoint {
    double x = 0.0;
    double y = 0.0;
};

// A cell of a band's grid. Floor division from the grid's origin, at least
// zero but not bounded above: a point is kept inside the picture before it
// gets here.
struct GridCell {
    int row = 0;
    int column = 0;
    bool operator==(const GridCell&) const = default;
};

bool gridded(const ScreenBand& band);
int column_at(const ScreenBand& band, int frame_x);
int row_at(const ScreenBand& band, int frame_y);
int frame_x_of(const ScreenBand& band, int column);
int frame_y_of(const ScreenBand& band, int row);

// The one band whose grid a selection snaps to: the gridded band holding the
// selection's top, else the first gridded band; null when none is gridded.
const ScreenBand* selection_band(const ScreenGrid& grid, double top_y);

// The two points' bounding box, outward to whole pixels: the anywhere
// marquee and what it reads.
PixelRect bounding_rect(FramePoint a, FramePoint b);

// The block of whole cells the two points touch: the columns selection and
// what it reads. Unsnapped without a gridded band.
PixelRect snapped_rectangle(FramePoint a, FramePoint b, const ScreenBand* band);

// A rows selection: from the earlier cell to the later in reading order,
// whichever way the drag went, and the region read for it, which is full
// width so whole intermediate rows come back and the ends are trimmed here.
struct RowsFlow {
    GridCell start;
    GridCell end;
    PixelRect request_region;
};

RowsFlow rows_flow(FramePoint anchor, FramePoint focus, const ScreenBand& band,
                   std::uint32_t frame_width);

// What is selected, before any text is read: the rows flow (its first row
// from the start cell to the right edge, whole rows between, the last row
// from the left edge to the end cell, each a row pitch tall so rows touch),
// the columns block, and nothing for anywhere, which has no grid.
std::vector<PixelRect> range_rects(Interpretation interpretation, FramePoint anchor,
                                   FramePoint focus, const ScreenBand* band,
                                   std::uint32_t frame_width);

// A rows reading trimmed to its flow: the runs keep only the characters
// (and their cells) inside it, and the text has one line per row of the
// flow, blank rows included, a row's runs joined in reading order.
struct RowsText {
    std::vector<TextRun> runs;
    std::string text;
};

RowsText trim_to_flow(const std::vector<TextRun>& runs, const ScreenBand& band,
                      const RowsFlow& flow);

// A run's text as one string per cell: a character may be several bytes
// (a Mode 7 screen copied as displayed is Unicode).
std::vector<std::string> characters(const std::string& text);

// The cells a reading lights, which are what a copy will take: for a run
// with cells, every matched cell from its first real glyph (matched, not a
// space) to its last, so spaces between words stay lit and a run with no
// real glyph lights nothing; a run without cells (teletext, exact and all
// matched) is lit whole.
std::vector<PixelRect> matched_cell_rects(const std::vector<TextRun>& runs);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_CONNECTION_SCREEN_SELECTION_HPP
