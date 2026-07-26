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

#include <beebium/TeletextGrid.hpp>

#include <cstddef>
#include <limits>
#include <string>

namespace beebium {

// A rectangle of cells, in grid coordinates.
//
// The default extent is the whole grid, whatever its captured size: rows and
// columns are a sentinel the reader clips to the actual dimensions, so a
// default-constructed region reads a custom-shaped screen in full rather than
// only its standard 40x25 corner.
struct TeletextRegion {
    size_t row = 0;
    size_t column = 0;
    size_t rows = std::numeric_limits<size_t>::max();
    size_t columns = std::numeric_limits<size_t>::max();

    static TeletextRegion whole_screen() { return {}; }

    bool operator==(const TeletextRegion&) const = default;
};

// How a region of cells becomes a run of text.
// Which repertoire gives the captured bytes their meaning.
//
// The bytes are the same either way; only what they are taken to mean differs.
// The SAA5050 draws eleven codes as characters ASCII puts elsewhere -- `[` is
// drawn as a left arrow, `^` as an up arrow, `~` as a division sign -- so a
// MODE 7 screen can be read two ways, and only the person reading it knows
// which they meant.
//
// Codes is the default because the commonest thing anyone copies off a BBC
// screen is a program listing, and MODE 7 is the default screen mode: `[` and
// `]` delimit assembler blocks in BBC BASIC and `^` is exponentiation, so
// reading them as arrows quietly corrupts the listing. Reporting the code is
// wrong only for someone capturing teletext as it looked, who can say so.
enum class TeletextCharacters {
    // What the byte is: `[`, `]`, `^`, `~` and the rest as ASCII.
    Codes,
    // What the screen showed: the SAA5050's own glyphs, as Unicode.
    Displayed,
};

enum class TeletextLinearisation {
    // Each row of the region contributes its own line: the shape of the
    // rectangle is preserved. This is what a column of figures wants.
    Rows,
    // The selection runs from the first cell to the last as the text reads,
    // wrapping at the region's right edge -- what a sentence spanning several
    // rows wants.
    Flowed,
};

// Convert a region of a captured MODE 7 screen to text.
//
// Copies what is displayed, which decides the awkward cases:
//
//   Graphics cells    become spaces. The mosaics have Unicode representations
//                     (the Symbols for Legacy Computing block) and a renderer
//                     should use them, but text pasted into a bug report or a
//                     source file should not be full of block characters.
//   Control codes     become spaces, preserving column alignment -- the main
//                     reason for copying a teletext screen at all.
//   Concealed cells   become spaces, because that is what is on screen.
//                     Revealing them here would leak what the display hides.
//   Double height     the top half gives the character, the bottom half a
//                     space, so a line is not duplicated.
//   Alpha characters  are mapped through the SAA5050's own repertoire, so a
//                     cell showing a pound sign copies as one. What you see is
//                     what you get.
//
// Trailing spaces are stripped from each line, and lines are joined with a
// single LF. LF rather than a platform-native ending on purpose: this is the
// canonical form for the wire, and a client converts at the point where text
// meets its own clipboard. The server cannot know the client's platform.
std::string teletext_text(const TeletextGrid& grid,
                          const TeletextRegion& region = TeletextRegion::whole_screen(),
                          TeletextLinearisation linearisation = TeletextLinearisation::Rows,
                          TeletextCharacters characters = TeletextCharacters::Codes);

// As above, for a snapshot taken off the emulation thread. Any reader outside
// that thread holds a snapshot rather than the live grid.
std::string teletext_text(const TeletextGrid::Snapshot& snapshot,
                          const TeletextRegion& region = TeletextRegion::whole_screen(),
                          TeletextLinearisation linearisation = TeletextLinearisation::Rows,
                          TeletextCharacters characters = TeletextCharacters::Codes);

// The Unicode codepoint a MODE 7 alpha character means, in the chosen
// repertoire.
//
// Under Displayed the SAA5050's UK repertoire applies: ASCII except for the
// eleven positions where the chip draws something else. Under Codes the byte
// is taken at face value, so those eleven read as the ASCII characters they
// are. Either way most characters map to themselves, and 0 comes back for a
// character with no sensible text form.
char32_t teletext_alpha_codepoint(
    uint8_t character,
    TeletextCharacters characters = TeletextCharacters::Codes);

// The Unicode codepoint a MODE 7 mosaic (sixel) character draws.
//
// A graphics cell divides into a 2x3 grid of blocks, and the code says which
// are lit: bits 0 and 1 are the top pair, 2 and 3 the middle, 4 and 6 the
// bottom. Bit 5 is what marks the code as graphics at all, which is why
// 0x40-0x5F are alphanumerics even in graphics mode.
//
// Unicode has exactly this, in Symbols for Legacy Computing: the block
// sextants at U+1FB00, which enumerate the sixty-four patterns minus the four
// that already had characters -- blank, both half blocks, and the full block.
// So the mapping is exact rather than approximate, and does not depend on a
// font: it is the same character.
//
// Contiguous and separated graphics map alike. Separation is how the chip
// draws a mosaic -- with gaps between the blocks -- and not a different
// character, in the way that italics are not different letters.
//
// Returns 0 for a code that is not a mosaic, which the caller renders as a
// space.
char32_t teletext_graphics_codepoint(uint8_t character);

// What one captured cell copies as: the single rule for reading a MODE 7 cell.
//
// Every reader of the grid goes through this, so that "what a cell copies as"
// has one answer rather than one per caller. It had two for a while -- the
// linear teletext_text() and the screen-text band reader -- kept in step by a
// comment, which is exactly as reliable as it sounds.
//
// Returns 0 for a cell with no text form, which the caller renders as a space
// so that what is copied lines up with what is displayed. That covers control
// codes, concealed cells, the lower half of a double-height row, and -- under
// Codes -- mosaics, whose byte is a graphics code and not text.
char32_t teletext_cell_codepoint(const TeletextCell& cell,
                                 TeletextCharacters characters);

} // namespace beebium
