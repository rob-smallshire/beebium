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

#ifndef SCREENTEXT_IMAGE_HPP
#define SCREENTEXT_IMAGE_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

namespace screentext {

// A greyscale or paletted image, one byte per pixel. What the values mean is
// decided by `Band::background`: the caller has already reduced whatever it
// had to single bytes, so the library never needs a palette.
struct Image {
    std::size_t width = 0;
    std::size_t height = 0;
    std::vector<std::uint8_t> pixels; // width * height, row-major

    std::uint8_t pixel(std::size_t x, std::size_t y) const
    {
        return pixels[y * width + x];
    }

    bool empty() const { return width == 0 || height == 0; }

    // True when `pixels` holds exactly width * height entries.
    bool consistent() const { return pixels.size() == width * height; }
};

struct Rect {
    std::size_t x = 0;
    std::size_t y = 0;
    std::size_t width = 0;
    std::size_t height = 0;

    std::size_t right() const { return x + width; }
    std::size_t bottom() const { return y + height; }
    bool empty() const { return width == 0 || height == 0; }

    // True when `inner` lies wholly within this rectangle. Cells are admitted
    // only on this basis: a cell straddling an edge is not matched.
    bool contains(const Rect& inner) const
    {
        return inner.x >= x && inner.y >= y && inner.right() <= right()
            && inner.bottom() <= bottom();
    }
};

// A horizontal band of the image sharing one character geometry.
//
// Bands exist because a display need not have one geometry throughout: the
// CRTC can be reprogrammed mid-frame, so the caller describes each band it
// found rather than one grid for the image. Cell sizes and grid origins are
// inputs; nothing here assumes 8x8.
struct Band {
    std::size_t top = 0;    // first image row in this band
    std::size_t bottom = 0; // one past the last

    std::size_t cell_width = 8;
    std::size_t cell_height = 8;

    // Where the character grid starts, in image coordinates. Cells step from
    // here by the cell size; those not wholly within the band are not matched.
    std::size_t origin_x = 0;
    std::size_t origin_y = 0;

    // The pixel value meaning "not part of a glyph". Every other value is
    // treated as foreground, which is what reduces a paletted cell to the one
    // bit per pixel a glyph is expressed in.
    std::uint8_t background = 0;

    std::size_t height() const { return bottom > top ? bottom - top : 0; }
};

} // namespace screentext

#endif // SCREENTEXT_IMAGE_HPP
