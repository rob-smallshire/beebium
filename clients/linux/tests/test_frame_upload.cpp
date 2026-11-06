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

// A frame's pixels go to the texture upload without a copy (client
// specification 6.1): one copy of a frame per presented frame was a fifth of
// the client's main thread on a Raspberry Pi.

#include "display/DisplayRenderer.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;

TEST_CASE("A frame's upload shares the frame's pixels rather than copying them",
          "[display][upload]") {
    DecodedFrame frame;
    frame.width = 640;
    frame.height = 256;
    frame.pixels.assign(std::size_t{640} * 256 * 4, '\x5a');
    const QRhiTextureSubresourceUploadDescription upload = frame_upload(frame);
    CHECK(upload.data().constData() == frame.pixels.data());
    CHECK(upload.data().size() == static_cast<qsizetype>(frame.pixels.size()));
    CHECK(upload.dataStride() == 640u * 4);
}
