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

#pragma once

#include <beebium/FrameBuffer.hpp>
#include <beebium/TeletextGrid.hpp>

#include <cstdint>
#include <string>
#include <vector>

// Reading text off the display, whatever mode is producing it.
//
// A band of scanlines is handed to whichever strategy can read it, and the runs
// the strategies produce are merged into one reading of the requested region.
// The teletext strategy lives here because MODE 7 characters are known exactly
// before any pixels exist; recognising glyphs in pixels is the business of the
// standalone screen-text library, reached through the same seam.
namespace beebium::screen {

// A rectangle in frame pixel coordinates. The origin is the top-left of the
// active area, matching the framebuffer the renderer writes, so a client that
// has mapped a drag into frame pixels can use it unchanged.
struct PixelRect {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;

    [[nodiscard]] uint32_t right() const { return x + width; }
    [[nodiscard]] uint32_t bottom() const { return y + height; }

    [[nodiscard]] bool empty() const { return width == 0 || height == 0; }

    // The overlap with another rectangle, empty when they do not meet.
    [[nodiscard]] PixelRect intersected(const PixelRect& other) const;

    bool operator==(const PixelRect&) const = default;
};

// Which of the two searches a glyph-recognising strategy should run.
//
// Recorded and passed to the strategies rather than acted on here. The teletext
// strategy reads the character grid and is unaffected by it; a strategy that
// recognises glyphs in pixels chooses between walking the grid and searching
// sub-cell offsets on the strength of it.
enum class Search {
    // All the text, wherever it sits. A strategy that reads glyphs from pixels
    // reads the grid and the off-grid text in disjoint passes and returns them
    // together; because the passes do not overlap, that is a concatenation, not
    // a reconciliation, and the result is a strict superset of Aligned.
    Anywhere,
    // Only text on the character grid: exact and cheaper.
    Aligned,
};

// How the runs are joined into one string.
enum class Layout {
    // Each grid row is its own line, preserving the shape of the selection.
    Rows,
    // A row that filled the region's width wrapped, so it continues into the
    // next without a break.
    Flowed,
};

// A contiguous piece of text and where it was found.
struct TextRun {
    std::string text;
    PixelRect bounds;

    // The cell geometry the run was read with, so a selection can snap to it.
    // Zero when the run is not cell-aligned, as text written at the graphics
    // cursor is.
    uint32_t cell_width = 0;
    uint32_t cell_height = 0;

    // True when the run reached the right edge of the region it was read from,
    // so Flowed layout knows the line continues rather than ends.
    bool reached_right_edge = false;

    bool operator==(const TextRun&) const = default;
};

// What one strategy made of one band.
struct BandReading {
    // False when no strategy could read the band. The band then contributes no
    // runs, and says so, rather than contributing something stale.
    bool supported = false;

    std::vector<TextRun> runs;

    // Cells a strategy tried to read and could not identify at all, and cells
    // it read but could not pin to a single character because the font draws
    // two the same. Both zero for teletext, whose cells are exact codes.
    uint32_t unreadable_cells = 0;
    uint32_t ambiguous_cells = 0;
};

// The reading of a whole request.
struct Reading {
    // True when at least one band in the region had a strategy that could read
    // it. A split screen with a MODE 7 band reads that band and reports
    // supported even while a band it cannot read contributes nothing.
    bool supported = false;

    std::vector<TextRun> runs;
    std::string text;

    uint32_t unreadable_cells = 0;
    uint32_t ambiguous_cells = 0;

    uint64_t frame_number = 0;
};

// One band of scanlines sharing a character geometry, in frame pixels.
//
// `is_teletext` and the geometry are recorded by the renderer as the frame is
// drawn, so a split screen carries one band per geometry rather than one per
// frame.
struct Band {
    uint32_t top = 0;      // First scanline, inclusive
    uint32_t bottom = 0;   // One past the last

    uint32_t cell_width = 0;
    uint32_t cell_height = 0;

    // Cell-to-cell step, equal to the cell size except where a mode leaves
    // blank scanlines between rows: MODE 3 and MODE 6 put an eight-scanline
    // glyph on a ten-scanline pitch, and the two spare lines are blanked
    // rather than painted with the background colour.
    uint32_t column_pitch = 0;
    uint32_t row_pitch = 0;

    uint32_t origin_x = 0;
    uint32_t origin_y = 0;

    // True when the SAA5050 was driving these scanlines.
    bool is_teletext = false;

    bool operator==(const Band&) const = default;
};

// The bands of a completed frame, derived from what the renderer recorded.
std::vector<Band> bands_of(const FrameMetadata& metadata);

// Read one band, choosing the strategy the band's hardware state calls for.
//
// The teletext strategy applies when the SAA5050 was driving the band. Nothing
// else applies yet, so any other band is unsupported and contributes no runs.
// A strategy is added by extending this dispatch, not by changing anything the
// callers or the wire see.
BandReading read_band(const Band& band,
                      const PixelRect& region,
                      Search search,
                      const TeletextGrid::Snapshot& teletext);

// Concatenate the bands' readings into one, in reading order, and linearise.
//
// Reading order is bands top to bottom, and within a band by baseline then x.
// Bands do not overlap and no strategy may produce overlapping runs, so this is
// plain concatenation: there is nothing to dedupe. When the bitmap strategy
// serves Anywhere it runs two passes over the band -- the grid and the off-grid
// search -- but the library builds those to be disjoint, so it concatenates
// them into one BandReading. There is nothing to reconcile at either level.
Reading concatenate_bands_readings(std::vector<BandReading> readings, Layout layout);

// Join runs into one string.
//
// Runs on the same baseline become one line, separated where a gap in the grid
// says they should be. Under Flowed a run that reached the right edge of its
// region continues into the next line rather than ending it. Trailing spaces
// are stripped from each line, trailing blank lines from the whole, and lines
// are joined with LF -- the canonical form for the wire, which a client
// converts where text meets its own clipboard.
std::string linearise(const std::vector<TextRun>& runs, Layout layout);

} // namespace beebium::screen
