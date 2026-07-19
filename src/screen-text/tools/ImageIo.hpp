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

#ifndef SCREENTEXT_TOOLS_IMAGEIO_HPP
#define SCREENTEXT_TOOLS_IMAGEIO_HPP

#include <string>

#include "screentext/Image.hpp"

namespace screentext::tools {

// Load an image and reduce it to one byte per pixel.
//
// Decoding lives here rather than in the library: a linked caller passes a
// buffer it already has, so only the CLI needs to read files, and the library
// itself carries no third-party dependency at all.
//
// Colour is reduced by taking the luminance of each pixel, so that any input
// format collapses to the single-byte image the library reads. Which value
// counts as background remains the caller's decision, expressed as
// `Band::background`.
//
// Returns false and sets `error` when the file cannot be read or decoded.
bool load_image(const std::string& filepath, Image& image, std::string& error);

} // namespace screentext::tools

#endif // SCREENTEXT_TOOLS_IMAGEIO_HPP
