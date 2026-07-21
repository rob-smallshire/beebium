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

#include <screentext/Glyph.hpp>

#include <cstdint>
#include <functional>
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

// One cell of a run: where it sits, and whether a glyph was recognised there.
// A genuine space is matched (it is a real character); an unmatched cell had
// ink no glyph fit and copies as a space. Reported so a client can highlight
// only what was read rather than the whole run, which spans unmatched cells.
struct TextCell {
    PixelRect bounds;
    bool matched = false;

    bool operator==(const TextCell&) const = default;
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

    // The run's cells in reading order, from the glyph-recognising strategy.
    // Empty from the teletext strategy, whose cells are exact characters and
    // all matched: a client then highlights the whole run's bounds.
    std::vector<TextCell> cells;

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

// The rendered pixels of a frame, as the framebuffer holds them: logical
// pixels, BGRA32, row-major, `stride` pixels between one row and the next.
//
// The bitmap strategy reduces these to the library's one-byte-per-pixel image,
// where equal bytes mean the same colour. Bitmap modes output discrete palette
// colours with no blending, so distinct pixel values are distinct colours and
// the reduction is exact.
struct FrameImage {
    const uint32_t* pixels = nullptr;
    uint32_t stride = 0;   // Pixels per row (the framebuffer's capacity width)
    uint32_t width = 0;    // Active frame width, in logical pixels
    uint32_t height = 0;   // Active frame height, in scanlines

    [[nodiscard]] uint32_t pixel(uint32_t x, uint32_t y) const {
        return pixels[y * stride + x];
    }

    [[nodiscard]] bool empty() const {
        return pixels == nullptr || width == 0 || height == 0;
    }
};

// What a strategy needs beyond the band it is reading, assembled once per
// request and handed to every band unchanged.
//
// The teletext strategy uses only the snapshot; the bitmap strategy uses the
// pixels and the glyph set. Bundling them keeps read_band's dispatch one
// function whatever a band turns out to need, and none of this crosses the
// wire -- read_band's callers are all in the core and the service.
struct BandSources {
    // The MODE 7 grid, as the teletext strategy reads it. Non-null whenever a
    // teletext band might be present.
    const TeletextGrid::Snapshot* teletext = nullptr;

    // The frame's rendered pixels, for the glyph-recognising strategy.
    FrameImage image;

    // Glyph sets in precedence order, later overriding earlier: the built-in
    // Acorn ROM font first, then the soft font read from RAM. Non-null for a
    // bitmap read.
    const std::vector<screentext::GlyphSet>* glyph_sets = nullptr;
};

// Read one band, choosing the strategy the band's hardware state calls for.
//
// The teletext strategy applies when the SAA5050 was driving the band; every
// other band is read by recognising glyphs in its pixels. The strategy is
// chosen from the band, never from anything a caller said, so a new strategy is
// added by extending this dispatch and nothing the callers or the wire see
// changes.
BandReading read_band(const Band& band,
                      const PixelRect& region,
                      Search search,
                      const BandSources& sources);

// Read the soft (VDU 23) font redefinitions out of guest RAM as a glyph set
// that overrides the ROM font where the running program has redefined a glyph.
//
// `peek_byte` reads a guest address without side effects, the way the debugger
// reaches memory. What is read is the VDU driver's own font workspace, so this
// resolves the font explode state exactly as the MOS would when drawing:
//
//   $0367  vduFontFlags          -- which of the seven 32-character zones have
//                                    RAM (soft) definitions rather than ROM
//   $0368  vduFontZoneAddressesHigh1..7 (through $036E) -- the page each zone's
//                                    definitions live on
//
// These locations are pinned to the MOS 1.20 (Model B) and MOS 2.00 (Model B+)
// family Beebium ships; they are NOT assumed to hold for a Master or any other
// OS, which may lay its workspace out differently. Callers must therefore read
// the soft font only for a recognised MOS -- assemble_glyph_sets does exactly
// that. A glyph left in ROM, or redefined to blank, is not emitted: the ROM
// base set already carries the former and an all-blank override would
// masquerade as a space and collide with every blank cell.
screentext::GlyphSet read_soft_font(
    const std::function<uint8_t(uint16_t)>& peek_byte);

// The glyph sets a bitmap band is read with: the ROM base font, overlaid with
// the soft font from RAM when the running MOS is one whose workspace we know.
//
// The base font is always the built-in Acorn set (MOS 1.20's, which is
// byte-for-byte MOS 2.00's). The soft font is added only once the running MOS
// is recognised by its ROM font; on an unrecognised OS the base set stands
// alone, so a redefinition there is declined rather than mis-read from
// workspace addresses that may not apply.
//
// DEFERRED -- the ROM base font is never read from the machine. On the OS ROMs
// Beebium ships the built-in set IS the machine's ROM font, so this is exact.
// But on a machine whose ROM font differs -- a Master (MOS 3.x), MOS 5.x, or a
// foreign OS -- text drawn in that font is matched against MOS 1.20's glyphs,
// so glyphs that differ do not match and read as unread (honest, but unread);
// today there is no live ROM-font read at all, only the built-in. Closing this
// means, at the recognition point in the implementation, reading the ROM font
// out of an unrecognised machine and using the built-in only when that too
// fails. It belongs with the wider multi-MOS work (the Master explodes the font
// into a different location and lays its VDU workspace out differently), so it
// is deferred until those machines are supported.
std::vector<screentext::GlyphSet> assemble_glyph_sets(
    const std::function<uint8_t(uint16_t)>& peek_byte);

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
