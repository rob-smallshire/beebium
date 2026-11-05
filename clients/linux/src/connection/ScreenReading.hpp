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

#ifndef BEEBIUM_LINUX_CONNECTION_SCREEN_READING_HPP
#define BEEBIUM_LINUX_CONNECTION_SCREEN_READING_HPP

#include "connection/DecodedFrame.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace beebium {
class GetScreenTextRequest;
class ScreenGeometry;
class ScreenHold;
class ScreenText;
}  // namespace beebium

namespace beebium::linux_client {

// The screen text calls (client specification 6.5) as the client uses them:
// HoldScreen's grid and still, GetScreenText's runs and cells, and the
// request each interpretation sends. All recognition is the server's.

// A rectangle in frame pixels, from the top-left of the active area
// (video.proto PixelRegion).
struct PixelRect {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool operator==(const PixelRect&) const = default;
};

// The character grid for one band of scanlines (video.proto
// ScreenBandGeometry): pitch is the cell-to-cell step, larger than the cell
// where a mode leaves blank scanlines between rows.
struct ScreenBand {
    std::uint32_t top = 0;     // inclusive
    std::uint32_t bottom = 0;  // exclusive
    std::uint32_t cell_width = 0;
    std::uint32_t cell_height = 0;
    std::uint32_t column_pitch = 0;
    std::uint32_t row_pitch = 0;
    std::uint32_t origin_x = 0;
    std::uint32_t origin_y = 0;
};

struct ScreenGrid {
    std::vector<ScreenBand> bands;
    std::uint64_t frame_number = 0;
};

// A cell of a run: matched when a glyph was recognised there (a genuine
// space included); unmatched ink copies as a space.
struct TextCell {
    PixelRect bounds;
    bool matched = false;
};

// A piece of text and where it was found. Its cells come from the glyph
// recogniser only; a teletext run has none and is wholly matched.
struct TextRun {
    std::string text;
    PixelRect bounds;
    std::uint32_t cell_width = 0;  // zero off the grid
    std::uint32_t cell_height = 0;
    std::vector<TextCell> cells;
};

struct ScreenReading {
    bool supported = false;
    std::vector<TextRun> runs;  // reading order
    std::string text;           // LF-joined by the server
    std::uint32_t unreadable_cells = 0;
    std::uint32_t ambiguous_cells = 0;
    std::uint64_t frame_number = 0;
};

// A held screen: the id later reads name, its grid, and the still when it
// was asked for.
struct HeldScreen {
    std::uint64_t hold_id = 0;
    ScreenGrid grid;
    std::unique_ptr<DecodedFrame> frame;
};

using HeldScreenResult = std::variant<HeldScreen, FrameDecodeError>;

// The three readings of a selection: aligned rows in reading order, an
// aligned rectangular block, and every glyph in the marquee on the grid or
// off it.
enum class Interpretation { rows, columns, anywhere };

// How a Mode 7 byte is copied: as its character code or as the glyph the
// screen displayed.
enum class Mode7Characters { codes, displayed };

ScreenGrid screen_grid(const beebium::ScreenGeometry& wire);
ScreenReading screen_reading(const beebium::ScreenText& wire);

// Consumes the still's pixels; a malformed still refuses the whole hold.
HeldScreenResult held_screen(beebium::ScreenHold& wire,
                             std::chrono::steady_clock::time_point received_at);

// GetScreenText for an interpretation, laid out by rows: the client builds
// its own lines from them. No region reads the whole display, no hold the
// live screen.
beebium::GetScreenTextRequest screen_text_request(Interpretation interpretation,
                                                  Mode7Characters characters,
                                                  std::optional<PixelRect> region,
                                                  std::optional<std::uint64_t> hold_id);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_CONNECTION_SCREEN_READING_HPP
