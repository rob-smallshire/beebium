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

#include "screentext/Bitmap.hpp"

#include <stdexcept>

namespace screentext {

namespace {

// Mask of the significant bits in the last byte of a row. All ones when the
// width is a multiple of eight.
std::uint8_t trailing_mask(std::size_t width)
{
    const std::size_t remainder = width % 8;
    if (remainder == 0) {
        return 0xFFU;
    }
    return static_cast<std::uint8_t>(0xFFU << (8 - remainder));
}

} // namespace

Bitmap::Bitmap(std::size_t width, std::size_t height)
    : width_(width), height_(height)
{
    bytes_.assign(height_ * bytes_per_row(), 0);
}

Bitmap Bitmap::from_rows(std::initializer_list<std::uint8_t> rows)
{
    return from_rows(std::vector<std::uint8_t>(rows));
}

Bitmap Bitmap::from_rows(const std::vector<std::uint8_t>& rows)
{
    Bitmap bitmap(8, rows.size());
    bitmap.bytes_ = rows;
    return bitmap;
}

bool Bitmap::pixel(std::size_t x, std::size_t y) const
{
    if (x >= width_ || y >= height_) {
        return false;
    }
    const std::uint8_t byte = bytes_[y * bytes_per_row() + x / 8];
    return (byte & (0x80U >> (x % 8))) != 0;
}

void Bitmap::set_pixel(std::size_t x, std::size_t y, bool value)
{
    if (x >= width_ || y >= height_) {
        throw std::out_of_range("Bitmap::set_pixel outside the bitmap");
    }
    std::uint8_t& byte = bytes_[y * bytes_per_row() + x / 8];
    const std::uint8_t mask = static_cast<std::uint8_t>(0x80U >> (x % 8));
    if (value) {
        byte = static_cast<std::uint8_t>(byte | mask);
    } else {
        byte = static_cast<std::uint8_t>(byte & ~mask);
    }
}

Bitmap Bitmap::inverted() const
{
    Bitmap result(width_, height_);
    for (std::size_t index = 0; index < bytes_.size(); ++index) {
        result.bytes_[index] = static_cast<std::uint8_t>(~bytes_[index]);
    }
    result.normalise();
    return result;
}

bool Bitmap::is_blank() const
{
    for (const std::uint8_t byte : bytes_) {
        if (byte != 0) {
            return false;
        }
    }
    return true;
}

void Bitmap::normalise()
{
    const std::size_t stride = bytes_per_row();
    if (stride == 0) {
        return;
    }
    bytes_.resize(height_ * stride, 0);

    const std::uint8_t mask = trailing_mask(width_);
    if (mask == 0xFFU) {
        return;
    }
    for (std::size_t row = 0; row < height_; ++row) {
        std::uint8_t& last = bytes_[row * stride + stride - 1];
        last = static_cast<std::uint8_t>(last & mask);
    }
}

bool operator==(const Bitmap& lhs, const Bitmap& rhs)
{
    return lhs.width_ == rhs.width_ && lhs.height_ == rhs.height_
        && lhs.bytes_ == rhs.bytes_;
}

} // namespace screentext

namespace std {

std::size_t hash<screentext::Bitmap>::operator()(
    const screentext::Bitmap& bitmap) const noexcept
{
    // FNV-1a. Chosen for being fixed by its definition: the same bitmap
    // hashes the same on every platform and every build, which a
    // library-supplied hash does not guarantee.
    std::uint64_t value = 1469598103934665603ULL;
    const auto mix = [&value](std::uint64_t byte) {
        value ^= byte;
        value *= 1099511628211ULL;
    };
    mix(static_cast<std::uint64_t>(bitmap.width()));
    mix(static_cast<std::uint64_t>(bitmap.height()));
    for (const std::uint8_t byte : bitmap.bytes()) {
        mix(byte);
    }
    return static_cast<std::size_t>(value);
}

} // namespace std
