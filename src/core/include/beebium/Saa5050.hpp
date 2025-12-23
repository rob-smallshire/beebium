#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <cmath>
#include "PixelBatch.hpp"

namespace beebium {

// Teletext character set types
enum class TeletextCharset : uint8_t {
    Alpha = 0,
    ContiguousGraphics = 1,
    SeparatedGraphics = 2
};

// Pre-computed expanded font table
// teletext_expanded_font[aa][charset][char][row]
// - aa: 0 = no antialiasing, 1 = antialiased
// - charset: Alpha, ContiguousGraphics, SeparatedGraphics
// - char: 96 printable characters (0x20-0x7F)
// - row: 20 rows (10 font rows × 2 for scanline doubling)
// Each row is 12 bits wide (6 original pixels × 2 for horizontal doubling)
inline uint16_t TELETEXT_EXPANDED_FONT[2][3][96][20];

// Gamma-corrected blend table for 6→8 pixel expansion
// blend_table[a][b] = gamma_correct((a + 2*b) / 3)
inline uint8_t TELETEXT_BLEND_TABLE[16][16];

// Forward declaration of raw font data
extern const uint8_t TELETEXT_FONT_RAW[96][10];

// Initialize the expanded font and blend tables at startup
struct TeletextFontInit {
    static constexpr double GAMMA = 2.2;

    TeletextFontInit() {
        init_blend_table();
        init_expanded_font();
    }

private:
    // Get raw font byte for a character
    static uint8_t get_raw_font_byte(TeletextCharset charset, uint8_t ch, unsigned row);

    // Get graphics character row (sixel pattern)
    static uint8_t get_graphics_row(uint8_t ch, unsigned row, bool separated);

    // Expand 6-bit font row to 12 bits (horizontal pixel doubling)
    static uint16_t get_doubled_row(TeletextCharset charset, uint8_t ch, unsigned y);

    // Apply antialiasing to doubled row
    static uint16_t get_aa_row(TeletextCharset charset, uint8_t ch, unsigned y);

    // Check if character should be antialiased
    static bool should_antialias(TeletextCharset charset, uint8_t ch);

    void init_blend_table() {
        for (int a = 0; a < 16; ++a) {
            for (int b = 0; b < 16; ++b) {
                // Gamma-correct blend: (a + 2*b) / 3 in linear space
                double a_linear = std::pow(a / 15.0, GAMMA);
                double b_linear = std::pow(b / 15.0, GAMMA);
                double blended = std::pow((a_linear + b_linear * 2) / 3.0, 1.0 / GAMMA);
                TELETEXT_BLEND_TABLE[a][b] = static_cast<uint8_t>(blended * 15.0 + 0.5);
            }
        }
    }

    void init_expanded_font() {
        for (int charset_idx = 0; charset_idx < 3; ++charset_idx) {
            auto charset = static_cast<TeletextCharset>(charset_idx);
            for (int ch = 0; ch < 96; ++ch) {
                for (int y = 0; y < 20; ++y) {
                    // Non-AA version
                    TELETEXT_EXPANDED_FONT[0][charset_idx][ch][y] =
                        get_doubled_row(charset, static_cast<uint8_t>(ch + 32), y);
                    // AA version
                    TELETEXT_EXPANDED_FONT[1][charset_idx][ch][y] =
                        get_aa_row(charset, static_cast<uint8_t>(ch + 32), y);
                }
            }
        }
    }
};

// Global initializer (runs at startup)
inline TeletextFontInit g_teletext_font_init;

// Teletext font data: 96 characters (0x20-0x7F), 10 rows each, 6 bits per row
// Bit 0 = leftmost pixel, bit 5 = rightmost pixel
// clang-format off
inline const uint8_t TELETEXT_FONT_RAW[96][10] = {
    // 0x20 ' '
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 0x21 '!'
    {0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x08, 0x00, 0x00},
    // 0x22 '"'
    {0x00, 0x14, 0x14, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 0x23 British Pound
    {0x00, 0x18, 0x24, 0x04, 0x0E, 0x04, 0x04, 0x3E, 0x00, 0x00},
    // 0x24 '$'
    {0x00, 0x1C, 0x2A, 0x0A, 0x1C, 0x28, 0x2A, 0x1C, 0x00, 0x00},
    // 0x25 '%'
    {0x00, 0x06, 0x26, 0x10, 0x08, 0x04, 0x32, 0x30, 0x00, 0x00},
    // 0x26 '&'
    {0x00, 0x04, 0x0A, 0x0A, 0x04, 0x2A, 0x12, 0x2C, 0x00, 0x00},
    // 0x27 '''
    {0x00, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 0x28 '('
    {0x00, 0x10, 0x08, 0x04, 0x04, 0x04, 0x08, 0x10, 0x00, 0x00},
    // 0x29 ')'
    {0x00, 0x04, 0x08, 0x10, 0x10, 0x10, 0x08, 0x04, 0x00, 0x00},
    // 0x2A '*'
    {0x00, 0x08, 0x2A, 0x1C, 0x08, 0x1C, 0x2A, 0x08, 0x00, 0x00},
    // 0x2B '+'
    {0x00, 0x00, 0x08, 0x08, 0x3E, 0x08, 0x08, 0x00, 0x00, 0x00},
    // 0x2C ','
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x04, 0x00},
    // 0x2D '-'
    {0x00, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 0x2E '.'
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00},
    // 0x2F '/'
    {0x00, 0x00, 0x20, 0x10, 0x08, 0x04, 0x02, 0x00, 0x00, 0x00},
    // 0x30 '0'
    {0x00, 0x08, 0x14, 0x22, 0x22, 0x22, 0x14, 0x08, 0x00, 0x00},
    // 0x31 '1'
    {0x00, 0x08, 0x0C, 0x08, 0x08, 0x08, 0x08, 0x1C, 0x00, 0x00},
    // 0x32 '2'
    {0x00, 0x1C, 0x22, 0x20, 0x18, 0x04, 0x02, 0x3E, 0x00, 0x00},
    // 0x33 '3'
    {0x00, 0x3E, 0x20, 0x10, 0x18, 0x20, 0x22, 0x1C, 0x00, 0x00},
    // 0x34 '4'
    {0x00, 0x10, 0x18, 0x14, 0x12, 0x3E, 0x10, 0x10, 0x00, 0x00},
    // 0x35 '5'
    {0x00, 0x3E, 0x02, 0x1E, 0x20, 0x20, 0x22, 0x1C, 0x00, 0x00},
    // 0x36 '6'
    {0x00, 0x18, 0x04, 0x02, 0x1E, 0x22, 0x22, 0x1C, 0x00, 0x00},
    // 0x37 '7'
    {0x00, 0x3E, 0x20, 0x10, 0x08, 0x04, 0x04, 0x04, 0x00, 0x00},
    // 0x38 '8'
    {0x00, 0x1C, 0x22, 0x22, 0x1C, 0x22, 0x22, 0x1C, 0x00, 0x00},
    // 0x39 '9'
    {0x00, 0x1C, 0x22, 0x22, 0x3C, 0x20, 0x10, 0x0C, 0x00, 0x00},
    // 0x3A ':'
    {0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00},
    // 0x3B ';'
    {0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x08, 0x08, 0x04, 0x00},
    // 0x3C '<'
    {0x00, 0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00},
    // 0x3D '='
    {0x00, 0x00, 0x00, 0x3E, 0x00, 0x3E, 0x00, 0x00, 0x00, 0x00},
    // 0x3E '>'
    {0x00, 0x04, 0x08, 0x10, 0x20, 0x10, 0x08, 0x04, 0x00, 0x00},
    // 0x3F '?'
    {0x00, 0x1C, 0x22, 0x10, 0x08, 0x08, 0x00, 0x08, 0x00, 0x00},
    // 0x40 '@'
    {0x00, 0x1C, 0x22, 0x3A, 0x2A, 0x3A, 0x02, 0x1C, 0x00, 0x00},
    // 0x41 'A'
    {0x00, 0x08, 0x14, 0x22, 0x22, 0x3E, 0x22, 0x22, 0x00, 0x00},
    // 0x42 'B'
    {0x00, 0x1E, 0x22, 0x22, 0x1E, 0x22, 0x22, 0x1E, 0x00, 0x00},
    // 0x43 'C'
    {0x00, 0x1C, 0x22, 0x02, 0x02, 0x02, 0x22, 0x1C, 0x00, 0x00},
    // 0x44 'D'
    {0x00, 0x1E, 0x22, 0x22, 0x22, 0x22, 0x22, 0x1E, 0x00, 0x00},
    // 0x45 'E'
    {0x00, 0x3E, 0x02, 0x02, 0x1E, 0x02, 0x02, 0x3E, 0x00, 0x00},
    // 0x46 'F'
    {0x00, 0x3E, 0x02, 0x02, 0x1E, 0x02, 0x02, 0x02, 0x00, 0x00},
    // 0x47 'G'
    {0x00, 0x1C, 0x22, 0x02, 0x02, 0x32, 0x22, 0x3C, 0x00, 0x00},
    // 0x48 'H'
    {0x00, 0x22, 0x22, 0x22, 0x3E, 0x22, 0x22, 0x22, 0x00, 0x00},
    // 0x49 'I'
    {0x00, 0x1C, 0x08, 0x08, 0x08, 0x08, 0x08, 0x1C, 0x00, 0x00},
    // 0x4A 'J'
    {0x00, 0x20, 0x20, 0x20, 0x20, 0x20, 0x22, 0x1C, 0x00, 0x00},
    // 0x4B 'K'
    {0x00, 0x22, 0x12, 0x0A, 0x06, 0x0A, 0x12, 0x22, 0x00, 0x00},
    // 0x4C 'L'
    {0x00, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x3E, 0x00, 0x00},
    // 0x4D 'M'
    {0x00, 0x22, 0x36, 0x2A, 0x2A, 0x22, 0x22, 0x22, 0x00, 0x00},
    // 0x4E 'N'
    {0x00, 0x22, 0x22, 0x26, 0x2A, 0x32, 0x22, 0x22, 0x00, 0x00},
    // 0x4F 'O'
    {0x00, 0x1C, 0x22, 0x22, 0x22, 0x22, 0x22, 0x1C, 0x00, 0x00},
    // 0x50 'P'
    {0x00, 0x1E, 0x22, 0x22, 0x1E, 0x02, 0x02, 0x02, 0x00, 0x00},
    // 0x51 'Q'
    {0x00, 0x1C, 0x22, 0x22, 0x22, 0x2A, 0x12, 0x2C, 0x00, 0x00},
    // 0x52 'R'
    {0x00, 0x1E, 0x22, 0x22, 0x1E, 0x0A, 0x12, 0x22, 0x00, 0x00},
    // 0x53 'S'
    {0x00, 0x1C, 0x22, 0x02, 0x1C, 0x20, 0x22, 0x1C, 0x00, 0x00},
    // 0x54 'T'
    {0x00, 0x3E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00},
    // 0x55 'U'
    {0x00, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x1C, 0x00, 0x00},
    // 0x56 'V'
    {0x00, 0x22, 0x22, 0x22, 0x14, 0x14, 0x08, 0x08, 0x00, 0x00},
    // 0x57 'W'
    {0x00, 0x22, 0x22, 0x22, 0x2A, 0x2A, 0x2A, 0x14, 0x00, 0x00},
    // 0x58 'X'
    {0x00, 0x22, 0x22, 0x14, 0x08, 0x14, 0x22, 0x22, 0x00, 0x00},
    // 0x59 'Y'
    {0x00, 0x22, 0x22, 0x14, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00},
    // 0x5A 'Z'
    {0x00, 0x3E, 0x20, 0x10, 0x08, 0x04, 0x02, 0x3E, 0x00, 0x00},
    // 0x5B Left Arrow
    {0x00, 0x00, 0x08, 0x04, 0x3E, 0x04, 0x08, 0x00, 0x00, 0x00},
    // 0x5C '1/2'
    {0x00, 0x02, 0x02, 0x02, 0x02, 0x1A, 0x20, 0x10, 0x08, 0x38},
    // 0x5D Right Arrow
    {0x00, 0x00, 0x08, 0x10, 0x3E, 0x10, 0x08, 0x00, 0x00, 0x00},
    // 0x5E Up Arrow
    {0x00, 0x00, 0x08, 0x1C, 0x2A, 0x08, 0x08, 0x00, 0x00, 0x00},
    // 0x5F '#'
    {0x00, 0x14, 0x14, 0x3E, 0x14, 0x3E, 0x14, 0x14, 0x00, 0x00},
    // 0x60 '_' (underscore-like bar)
    {0x00, 0x00, 0x00, 0x00, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 0x61 'a'
    {0x00, 0x00, 0x00, 0x1C, 0x20, 0x3C, 0x22, 0x3C, 0x00, 0x00},
    // 0x62 'b'
    {0x00, 0x02, 0x02, 0x1E, 0x22, 0x22, 0x22, 0x1E, 0x00, 0x00},
    // 0x63 'c'
    {0x00, 0x00, 0x00, 0x3C, 0x02, 0x02, 0x02, 0x3C, 0x00, 0x00},
    // 0x64 'd'
    {0x00, 0x20, 0x20, 0x3C, 0x22, 0x22, 0x22, 0x3C, 0x00, 0x00},
    // 0x65 'e'
    {0x00, 0x00, 0x00, 0x1C, 0x22, 0x3E, 0x02, 0x1C, 0x00, 0x00},
    // 0x66 'f'
    {0x00, 0x10, 0x08, 0x08, 0x1C, 0x08, 0x08, 0x08, 0x00, 0x00},
    // 0x67 'g'
    {0x00, 0x00, 0x00, 0x3C, 0x22, 0x22, 0x22, 0x3C, 0x20, 0x1C},
    // 0x68 'h'
    {0x00, 0x02, 0x02, 0x1E, 0x22, 0x22, 0x22, 0x22, 0x00, 0x00},
    // 0x69 'i'
    {0x00, 0x08, 0x00, 0x0C, 0x08, 0x08, 0x08, 0x1C, 0x00, 0x00},
    // 0x6A 'j'
    {0x00, 0x08, 0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x04},
    // 0x6B 'k'
    {0x00, 0x04, 0x04, 0x24, 0x14, 0x0C, 0x14, 0x24, 0x00, 0x00},
    // 0x6C 'l'
    {0x00, 0x0C, 0x08, 0x08, 0x08, 0x08, 0x08, 0x1C, 0x00, 0x00},
    // 0x6D 'm'
    {0x00, 0x00, 0x00, 0x16, 0x2A, 0x2A, 0x2A, 0x2A, 0x00, 0x00},
    // 0x6E 'n'
    {0x00, 0x00, 0x00, 0x1E, 0x22, 0x22, 0x22, 0x22, 0x00, 0x00},
    // 0x6F 'o'
    {0x00, 0x00, 0x00, 0x1C, 0x22, 0x22, 0x22, 0x1C, 0x00, 0x00},
    // 0x70 'p'
    {0x00, 0x00, 0x00, 0x1E, 0x22, 0x22, 0x22, 0x1E, 0x02, 0x02},
    // 0x71 'q'
    {0x00, 0x00, 0x00, 0x3C, 0x22, 0x22, 0x22, 0x3C, 0x20, 0x20},
    // 0x72 'r'
    {0x00, 0x00, 0x00, 0x34, 0x0C, 0x04, 0x04, 0x04, 0x00, 0x00},
    // 0x73 's'
    {0x00, 0x00, 0x00, 0x3C, 0x02, 0x1C, 0x20, 0x1E, 0x00, 0x00},
    // 0x74 't'
    {0x00, 0x08, 0x08, 0x1C, 0x08, 0x08, 0x08, 0x10, 0x00, 0x00},
    // 0x75 'u'
    {0x00, 0x00, 0x00, 0x22, 0x22, 0x22, 0x22, 0x3C, 0x00, 0x00},
    // 0x76 'v'
    {0x00, 0x00, 0x00, 0x22, 0x22, 0x14, 0x14, 0x08, 0x00, 0x00},
    // 0x77 'w'
    {0x00, 0x00, 0x00, 0x22, 0x22, 0x2A, 0x2A, 0x14, 0x00, 0x00},
    // 0x78 'x'
    {0x00, 0x00, 0x00, 0x22, 0x14, 0x08, 0x14, 0x22, 0x00, 0x00},
    // 0x79 'y'
    {0x00, 0x00, 0x00, 0x22, 0x22, 0x22, 0x22, 0x3C, 0x20, 0x1C},
    // 0x7A 'z'
    {0x00, 0x00, 0x00, 0x3E, 0x10, 0x08, 0x04, 0x3E, 0x00, 0x00},
    // 0x7B '1/4'
    {0x00, 0x04, 0x04, 0x04, 0x04, 0x24, 0x30, 0x28, 0x38, 0x20},
    // 0x7C '||' (double bar)
    {0x00, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x00, 0x00},
    // 0x7D '3/4'
    {0x00, 0x0C, 0x10, 0x0C, 0x10, 0x2C, 0x30, 0x28, 0x38, 0x20},
    // 0x7E Divide
    {0x00, 0x00, 0x08, 0x00, 0x3E, 0x00, 0x08, 0x00, 0x00, 0x00},
    // 0x7F Block
    {0x00, 0x3E, 0x3E, 0x3E, 0x3E, 0x3E, 0x3E, 0x3E, 0x00, 0x00},
};
// clang-format on

// Implementation of TeletextFontInit static methods
inline uint8_t TeletextFontInit::get_graphics_row(uint8_t ch, unsigned row, bool separated) {
    // Sixel layout: bits 0,1 = top, 2,3 = middle, 4,6 = bottom
    uint8_t lmask, rmask;
    if (row < 3) {
        lmask = 0x01;
        rmask = 0x02;
    } else if (row < 7) {
        lmask = 0x04;
        rmask = 0x08;
    } else {
        lmask = 0x10;
        rmask = 0x40;
    }

    uint8_t result = 0;
    if (ch & lmask) result |= 0x07;  // Left 3 bits
    if (ch & rmask) result |= 0x38;  // Right 3 bits

    if (separated) {
        result &= ~0x09;  // Blank columns 0 and 3
        if (row == 2 || row == 6 || row == 9) {
            result = 0;
        }
    }
    return result;
}

inline uint8_t TeletextFontInit::get_raw_font_byte(TeletextCharset charset, uint8_t ch, unsigned row) {
    if (row >= 10) return 0;
    if (ch < 32 || ch >= 128) return 0;

    if (charset == TeletextCharset::Alpha) {
        return TELETEXT_FONT_RAW[ch - 32][row];
    } else if (ch & 0x20) {
        // Graphics character with bit 5 set (0x20-0x3F, 0x60-0x7F) - use sixel graphics
        bool separated = (charset == TeletextCharset::SeparatedGraphics);
        return get_graphics_row(ch, row, separated);
    } else {
        // Alpha character in graphics mode (0x40-0x5F range) - use font
        return TELETEXT_FONT_RAW[ch - 32][row];
    }
}

inline bool TeletextFontInit::should_antialias(TeletextCharset charset, uint8_t ch) {
    if (charset == TeletextCharset::Alpha) {
        return true;  // Always AA alpha characters
    }
    // For graphics, only AA if bit 5 is clear (actual graphics, not alpha)
    return !(ch & 0x20);
}

inline uint16_t TeletextFontInit::get_doubled_row(TeletextCharset charset, uint8_t ch, unsigned y) {
    if (y >= 20) return 0;

    // Vertical doubling: y/2 gives the original font row
    uint8_t byte = get_raw_font_byte(charset, ch, y / 2);

    // Horizontal doubling: each of 6 bits becomes 2 adjacent bits
    uint16_t w = 0;
    for (int i = 0; i < 6; ++i) {
        if (byte & (1 << i)) {
            w |= (3 << (i * 2));  // Set 2 adjacent bits
        }
    }
    return w;
}

inline uint16_t TeletextFontInit::get_aa_row(TeletextCharset charset, uint8_t ch, unsigned y) {
    if (!should_antialias(charset, ch)) {
        return get_doubled_row(charset, ch, y);
    }

    uint16_t a = get_doubled_row(charset, ch, y);
    // Adjacent row: y-1 for even rows, y+1 for odd rows
    unsigned adj_y = y - 1 + (y % 2) * 2;
    uint16_t b = get_doubled_row(charset, ch, adj_y);

    // B2's AA formula: add a pixel where current row has pixel,
    // adjacent row has pixel, but adjacent row's neighbor doesn't
    return a | (a >> 1 & b & ~(b >> 1)) | (a << 1 & b & ~(b << 1));
}

// SAA5050 Teletext character generator chip
class Saa5050 {
public:
    Saa5050() {
        reset();
    }

    void reset() {
        m_raster = 0;
        m_frame = 0;
        m_fg = 7;
        m_bg = 0;
        m_charset = TeletextCharset::Alpha;
        m_graphics_charset = TeletextCharset::ContiguousGraphics;
        m_conceal = false;
        m_hold = false;
        m_text_visible = true;
        m_frame_flash_visible = true;
        m_any_double_height = false;
        m_raster_shift = 0;
        m_raster_offset = 0;
        m_last_graphics_data = 0;
        m_write_index = 4;
        m_read_index = 0;
        std::memset(m_output, 0, sizeof(m_output));
    }

    // Set the current raster (scanline within character row) from CRTC
    // This should be called each tick with the CRTC's raster output
    void set_raster(uint8_t raster) {
        m_raster = raster;
    }

    // Feed a byte from screen memory (character code or control code)
    // dispen: 1 if display is enabled, 0 for blanking
    // Writes 2 Output entries (left and right halves of the 12-bit expanded font row)
    void byte(uint8_t value, uint8_t dispen) {
        value &= 0x7F;

        uint16_t data;  // 12-bit expanded font row

        if (value < 32) {
            // Control code - display as space (or held graphics)
            if (m_conceal || !m_hold) {
                data = 0;
            } else {
                data = m_last_graphics_data;
            }

            // Process control code (may modify data for hold graphics)
            uint8_t temp_data = static_cast<uint8_t>(data);
            process_control_code(value, temp_data);
            data = temp_data;

            if (!m_hold) {
                m_last_graphics_data = 0;
            }
        } else {
            // Display character using pre-computed expanded AA font
            uint8_t glyph_raster = (m_raster + m_raster_offset) >> m_raster_shift;

            if (glyph_raster < 20 && m_text_visible && !m_conceal) {
                // Use pre-computed expanded AA font
                int charset_idx = static_cast<int>(m_charset);
                data = TELETEXT_EXPANDED_FONT[1][charset_idx][value - 32][glyph_raster];
            } else {
                data = 0;
            }

            // Store graphics data for hold mode
            if ((value & 0x20) && m_charset != TeletextCharset::Alpha) {
                if (!m_conceal) {
                    m_last_graphics_data = data;
                }
            }
        }

        if (!dispen) {
            data = 0;
        }

        // Write 2 Output entries: left 6 bits and right 6 bits
        Output* output = &m_output[m_write_index & 7];
        output->fg = m_fg;
        output->bg = m_bg;
        output->data = static_cast<uint8_t>(data & 0x3F);  // Left 6 bits

        m_write_index = (m_write_index + 1) & 7;

        output = &m_output[m_write_index & 7];
        output->fg = m_fg;
        output->bg = m_bg;
        output->data = static_cast<uint8_t>((data >> 6) & 0x3F);  // Right 6 bits

        m_write_index = (m_write_index + 1) & 7;
    }

    // Emit pixels for half a character (8 output pixels from 6 font bits)
    // Uses gamma-corrected blending for the 6→8 pixel expansion
    // Pattern: pixel 0=p0, 1=blend(p0,p1), 2=blend(p2,p1), 3=p2, 4=p3, 5=blend(p3,p4), 6=blend(p5,p4), 7=p5
    void emit_pixels(PixelBatch& batch, const VideoDataPixel* palette) {
        Output* output = &m_output[m_read_index];

        VideoDataPixel bg_color = palette[output->bg];
        VideoDataPixel fg_color = palette[output->fg];
        uint8_t data = output->data;

        // Decode 6 font bits to fg/bg colors
        VideoDataPixel p[6];
        for (int i = 0; i < 6; ++i) {
            p[i] = (data >> i) & 1 ? fg_color : bg_color;
        }

        // Helper to blend two pixels using gamma-corrected table
        auto blend = [](VideoDataPixel a, VideoDataPixel b) -> VideoDataPixel {
            VideoDataPixel result;
            result.bits.r = TELETEXT_BLEND_TABLE[a.bits.r][b.bits.r];
            result.bits.g = TELETEXT_BLEND_TABLE[a.bits.g][b.bits.g];
            result.bits.b = TELETEXT_BLEND_TABLE[a.bits.b][b.bits.b];
            result.bits.x = 0;
            return result;
        };

        // Expand 6 pixels → 8 with weighted averaging
        // Pattern: 000 011 112 222 333 344 445 555
        batch.pixels.pixels[0] = p[0];           // Pure pixel 0
        batch.pixels.pixels[1] = blend(p[0], p[1]); // 1/3 p0 + 2/3 p1
        batch.pixels.pixels[2] = blend(p[2], p[1]); // 1/3 p2 + 2/3 p1
        batch.pixels.pixels[3] = p[2];           // Pure pixel 2
        batch.pixels.pixels[4] = p[3];           // Pure pixel 3
        batch.pixels.pixels[5] = blend(p[3], p[4]); // 1/3 p3 + 2/3 p4
        batch.pixels.pixels[6] = blend(p[5], p[4]); // 1/3 p5 + 2/3 p4
        batch.pixels.pixels[7] = p[5];           // Pure pixel 5

        batch.set_type(PixelBatchType::Teletext);
        m_read_index = (m_read_index + 1) & 7;
    }

    // Called at start of each scanline
    void start_of_line() {
        m_conceal = false;
        m_fg = 7;
        m_bg = 0;
        m_graphics_charset = TeletextCharset::ContiguousGraphics;
        m_charset = TeletextCharset::Alpha;
        m_last_graphics_data = 0;
        m_hold = false;
        m_text_visible = true;
        m_raster_shift = 0;

        m_read_index = 0;
        m_write_index = 4;
        std::memset(m_output, 0, sizeof(m_output));
    }

    // Called at end of each scanline (for double-height tracking)
    // Raster is now provided by CRTC via set_raster(), not tracked internally
    void end_of_line() {
        m_bg = 0;
        // Double-height handling: when CRTC raster wraps (new character row),
        // check if we need to show bottom half of double-height chars
        // This is detected when set_raster() sees raster go from high to 0
    }

    // Called at vertical sync
    void vsync() {
        m_raster = 0;
        ++m_frame;
        if (m_frame >= 64) {
            m_frame = 0;
        }
        m_frame_flash_visible = m_frame >= 16;
        m_any_double_height = false;
        m_raster_offset = 0;
    }

    // State accessors
    uint8_t foreground() const { return m_fg; }
    uint8_t background() const { return m_bg; }
    uint8_t raster() const { return m_raster; }
    TeletextCharset charset() const { return m_charset; }
    bool is_flash_enabled() const { return !m_text_visible; }

private:
    // Output entry stores 6 bits of font data plus colors
    // Each character produces 2 Output entries (left half and right half)
    struct Output {
        uint8_t fg;    // Foreground color index (0-7)
        uint8_t bg;    // Background color index (0-7)
        uint8_t data;  // 6 bits of expanded font row
    };

    void process_control_code(uint8_t code, uint8_t& data) {
        switch (code) {
            case 0x01: case 0x02: case 0x03:
            case 0x04: case 0x05: case 0x06: case 0x07:
                // Alpha colors
                m_fg = code;
                m_charset = TeletextCharset::Alpha;
                m_conceal = false;
                m_last_graphics_data = 0;
                break;

            case 0x08:
                // Flash
                m_text_visible = m_frame_flash_visible;
                break;

            case 0x09:
                // Steady
                m_text_visible = true;
                break;

            case 0x0C:
                // Normal Height
                if (m_raster_shift != 0) {
                    data = 0;
                    m_last_graphics_data = 0;
                }
                m_raster_shift = 0;
                break;

            case 0x0D:
                // Double Height
                if (m_raster_shift != 1) {
                    data = 0;
                    m_last_graphics_data = 0;
                }
                m_any_double_height = true;
                m_raster_shift = 1;
                break;

            case 0x11: case 0x12: case 0x13:
            case 0x14: case 0x15: case 0x16: case 0x17:
                // Graphics colors
                m_fg = code & 7;
                m_conceal = false;
                m_charset = m_graphics_charset;
                break;

            case 0x18:
                // Conceal Display
                m_conceal = true;
                break;

            case 0x19:
                // Contiguous Graphics
                m_graphics_charset = TeletextCharset::ContiguousGraphics;
                if (m_charset == TeletextCharset::SeparatedGraphics) {
                    m_charset = m_graphics_charset;
                }
                break;

            case 0x1A:
                // Separated Graphics
                m_graphics_charset = TeletextCharset::SeparatedGraphics;
                if (m_charset == TeletextCharset::ContiguousGraphics) {
                    m_charset = m_graphics_charset;
                }
                break;

            case 0x1C:
                // Black Background
                m_bg = 0;
                break;

            case 0x1D:
                // New Background
                m_bg = m_fg;
                break;

            case 0x1E:
                // Hold Graphics
                m_hold = true;
                data = m_last_graphics_data;
                break;

            case 0x1F:
                // Release Graphics
                m_hold = false;
                break;

            default:
                // Other codes (NUL, End Box, Start Box, etc.) - no action
                break;
        }
    }

    uint8_t get_glyph_row(uint8_t ch, uint8_t row) const {
        if (row >= 10) return 0;

        if (m_charset == TeletextCharset::Alpha || (ch & 0x20)) {
            // Alpha character - use font table
            return TELETEXT_FONT_RAW[ch - 32][row];
        } else {
            // Graphics character - generate from sixel bits
            return get_graphics_row(ch, row);
        }
    }

    uint8_t get_graphics_row(uint8_t ch, uint8_t row) const {
        // Sixel layout: bits 0,1 = top, 2,3 = middle, 4,6 = bottom
        uint8_t lmask, rmask;
        if (row < 3) {
            lmask = 0x01;
            rmask = 0x02;
        } else if (row < 7) {
            lmask = 0x04;
            rmask = 0x08;
        } else {
            lmask = 0x10;
            rmask = 0x40;
        }

        uint8_t result = 0;
        if (ch & lmask) result |= 0x07;  // Left 3 bits
        if (ch & rmask) result |= 0x38;  // Right 3 bits

        // Apply separation for separated graphics
        if (m_charset == TeletextCharset::SeparatedGraphics) {
            // Blank columns 0 and 3, blank rows 2, 6, 9
            result &= ~0x09;  // Blank columns 0 and 3
            if (row == 2 || row == 6 || row == 9) {
                result = 0;
            }
        }

        return result;
    }

    // Output delay buffer (4-slot delay for 2us LOSE-to-display)
    Output m_output[8] = {};
    uint8_t m_write_index = 4;
    uint8_t m_read_index = 0;

    // Raster state
    uint8_t m_raster = 0;
    uint8_t m_frame = 0;

    // Current colors
    uint8_t m_fg = 7;
    uint8_t m_bg = 0;

    // Character set state
    TeletextCharset m_charset = TeletextCharset::Alpha;
    TeletextCharset m_graphics_charset = TeletextCharset::ContiguousGraphics;

    // Graphics hold state (12-bit expanded font data)
    uint16_t m_last_graphics_data = 0;

    // Double height state
    uint8_t m_raster_shift = 0;
    uint8_t m_raster_offset = 0;
    bool m_any_double_height = false;

    // Display state
    bool m_conceal = false;
    bool m_hold = false;
    bool m_text_visible = true;
    bool m_frame_flash_visible = true;
};

} // namespace beebium
