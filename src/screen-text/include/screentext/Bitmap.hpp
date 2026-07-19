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

#ifndef SCREENTEXT_BITMAP_HPP
#define SCREENTEXT_BITMAP_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <vector>

namespace screentext {

// A monochrome bitmap, row-major, one bit per pixel, most significant bit
// leftmost -- the layout a font ROM dump has.
//
// Each row occupies `bytes_per_row()` bytes. Where `width` is not a multiple
// of eight the trailing bits of each row are padding and are always zero, so
// that two bitmaps of the same dimensions compare equal exactly when their
// pixels do. `normalise()` establishes that invariant and every operation
// here maintains it.
//
// Sizes are not fixed. A BBC character cell is 8x8 in every mode, but this is
// not a BBC library and the cell geometry is an input throughout.
class Bitmap {
public:
    Bitmap() = default;
    Bitmap(std::size_t width, std::size_t height);

    // Construct from one byte per row, eight pixels wide -- the common case,
    // and the shape of a font ROM dump.
    static Bitmap from_rows(std::initializer_list<std::uint8_t> rows);
    static Bitmap from_rows(const std::vector<std::uint8_t>& rows);

    std::size_t width() const { return width_; }
    std::size_t height() const { return height_; }
    std::size_t bytes_per_row() const { return (width_ + 7) / 8; }
    bool empty() const { return width_ == 0 || height_ == 0; }

    const std::vector<std::uint8_t>& bytes() const { return bytes_; }

    bool pixel(std::size_t x, std::size_t y) const;
    void set_pixel(std::size_t x, std::size_t y, bool value);

    // The complement within `width`; padding bits stay zero. This is how
    // inverse video is matched: the BBC produces it by swapping foreground
    // and background, which inverts the cell relative to the glyph.
    Bitmap inverted() const;

    // True when no pixel is set. A blank cell and the space glyph are both
    // all-zero, which is why they match one another exactly.
    bool is_blank() const;

    // Force padding bits to zero. Applied on construction; needed only after
    // writing raw bytes.
    void normalise();

    friend bool operator==(const Bitmap& lhs, const Bitmap& rhs);
    friend bool operator!=(const Bitmap& lhs, const Bitmap& rhs)
    {
        return !(lhs == rhs);
    }

private:
    std::size_t width_ = 0;
    std::size_t height_ = 0;
    std::vector<std::uint8_t> bytes_;
};

} // namespace screentext

namespace std {

template <>
struct hash<screentext::Bitmap> {
    std::size_t operator()(const screentext::Bitmap& bitmap) const noexcept;
};

} // namespace std

#endif // SCREENTEXT_BITMAP_HPP
