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

#include "ImageIo.hpp"

#include <cstdio>

#include "stb_image.h"

namespace screentext::tools {

bool load_image(const std::string& filepath, Image& image, std::string& error)
{
    int width = 0;
    int height = 0;
    int channels = 0;

    // Ask for greyscale. stb reduces colour by luminance, which is what a
    // paletted screenshot needs: distinct colours stay distinct from the
    // background, and the band's background value decides the rest.
    unsigned char* data
        = stbi_load(filepath.c_str(), &width, &height, &channels, 1);
    if (data == nullptr) {
        const char* reason = stbi_failure_reason();
        error = "cannot read " + filepath + ": "
            + (reason != nullptr ? reason : "unrecognised image format");
        return false;
    }

    image.width = static_cast<std::size_t>(width);
    image.height = static_cast<std::size_t>(height);
    image.pixels.assign(data, data + image.width * image.height);

    stbi_image_free(data);
    return true;
}

} // namespace screentext::tools
