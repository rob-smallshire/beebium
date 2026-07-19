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

#ifndef SCREENTEXT_TESTS_CANVAS_HPP
#define SCREENTEXT_TESTS_CANVAS_HPP

#include <cstdint>
#include <stdexcept>
#include <string>

#include "screentext/ScreenText.hpp"

namespace screentext::testing {

// Builds images by stamping known glyphs at known positions, so that every
// case is constructed exactly and no fixture file is needed.
class Canvas {
public:
    Canvas(std::size_t width, std::size_t height, std::uint8_t background = 0)
    {
        image_.width = width;
        image_.height = height;
        image_.pixels.assign(width * height, background);
    }

    const Image& image() const { return image_; }
    Image& image() { return image_; }

    void set_pixel(std::size_t x, std::size_t y, std::uint8_t value)
    {
        image_.pixels[y * image_.width + x] = value;
    }

    // Fill a rectangle with one value, for painting a cell's background
    // before a glyph is stamped over it.
    void fill(std::size_t x,
              std::size_t y,
              std::size_t width,
              std::size_t height,
              std::uint8_t value)
    {
        for (std::size_t row = 0; row < height; ++row) {
            for (std::size_t column = 0; column < width; ++column) {
                set_pixel(x + column, y + row, value);
            }
        }
    }

    // Stamp a bitmap with its top-left corner at (x, y). Set bits take
    // `foreground`; clear bits are left alone, so glyphs can be drawn over
    // whatever is already there.
    void stamp(std::size_t x,
               std::size_t y,
               const Bitmap& bitmap,
               std::uint8_t foreground = 1)
    {
        for (std::size_t row = 0; row < bitmap.height(); ++row) {
            for (std::size_t column = 0; column < bitmap.width(); ++column) {
                if (bitmap.pixel(column, row)) {
                    set_pixel(x + column, y + row, foreground);
                }
            }
        }
    }

    // Stamp a bitmap inverted: clear bits take `foreground`, set bits take
    // the background. This is what the BBC does for inverse video.
    void stamp_inverted(std::size_t x,
                        std::size_t y,
                        const Bitmap& bitmap,
                        std::uint8_t foreground = 1)
    {
        stamp(x, y, bitmap.inverted(), foreground);
    }

    // Stamp a string of ASCII text from a glyph set, one cell apart.
    void stamp_text(std::size_t x,
                    std::size_t y,
                    std::string_view text,
                    const GlyphSet& set,
                    std::size_t cell_width = 8,
                    std::uint8_t foreground = 1)
    {
        std::size_t pen = x;
        for (const char character : text) {
            stamp(pen, y, glyph_for(set, static_cast<char32_t>(character)),
                  foreground);
            pen += cell_width;
        }
    }

    static const Bitmap& glyph_for(const GlyphSet& set, char32_t codepoint)
    {
        for (const Glyph& glyph : set.glyphs) {
            if (glyph.codepoint == codepoint) {
                return glyph.bitmap;
            }
        }
        throw std::runtime_error("no glyph for codepoint in set " + set.name);
    }

private:
    Image image_;
};

// A single band covering the whole of an image, with the ordinary geometry.
inline Band whole_image_band(const Image& image,
                             std::size_t cell_width = 8,
                             std::size_t cell_height = 8)
{
    Band band;
    band.top = 0;
    band.bottom = image.height;
    band.cell_width = cell_width;
    band.cell_height = cell_height;
    return band;
}

} // namespace screentext::testing

#endif // SCREENTEXT_TESTS_CANVAS_HPP
