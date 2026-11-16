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

#include "Inflate.hpp"

#include <zlib.h>

namespace beebium::cassette_detail {

std::optional<std::vector<uint8_t>> inflate_all(std::span<const uint8_t> input, Container container) {
    z_stream stream{};
    // 16 + MAX_WBITS selects the gzip wrapper; MAX_WBITS the zlib one.
    const int window_bits = container == Container::Gzip ? 16 + MAX_WBITS : MAX_WBITS;
    if (inflateInit2(&stream, window_bits) != Z_OK) {
        return std::nullopt;
    }
    stream.next_in = const_cast<Bytef*>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());

    std::vector<uint8_t> output;
    uint8_t chunk[65536];
    int result = Z_OK;
    while (result != Z_STREAM_END) {
        stream.next_out = chunk;
        stream.avail_out = sizeof(chunk);
        result = inflate(&stream, Z_NO_FLUSH);
        if (result != Z_OK && result != Z_STREAM_END) {
            inflateEnd(&stream);
            return std::nullopt;
        }
        output.insert(output.end(), chunk, chunk + (sizeof(chunk) - stream.avail_out));
        if (result == Z_OK && stream.avail_in == 0 && stream.avail_out != 0) {
            // Input exhausted before the end of the stream: truncated.
            inflateEnd(&stream);
            return std::nullopt;
        }
    }
    inflateEnd(&stream);
    return output;
}

std::vector<uint8_t> deflate_gzip(std::span<const uint8_t> input) {
    z_stream stream{};
    // 15 window bits plus 16: a gzip wrapper. zlib writes a zero time and no
    // name unless asked.
    if (deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, 15 + 16, 9, Z_DEFAULT_STRATEGY) != Z_OK) {
        return {};
    }
    std::vector<uint8_t> out(deflateBound(&stream, static_cast<uLong>(input.size())));
    stream.next_in = const_cast<Bytef*>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = out.data();
    stream.avail_out = static_cast<uInt>(out.size());
    const int result = deflate(&stream, Z_FINISH);
    out.resize(stream.total_out);
    deflateEnd(&stream);
    return result == Z_STREAM_END ? out : std::vector<uint8_t>{};
}

}  // namespace beebium::cassette_detail
