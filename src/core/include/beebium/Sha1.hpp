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

#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace beebium {

// SHA-1 (FIPS 180-4), for identifying ROM images by their contents. Not for
// anything that needs a secure hash.
inline std::array<uint8_t, 20> sha1(std::span<const uint8_t> data) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    auto rotl = [](uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };

    auto process = [&](const uint8_t* block) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t{block[4 * i]} << 24) | (uint32_t{block[4 * i + 1]} << 16) |
                   (uint32_t{block[4 * i + 2]} << 8) | uint32_t{block[4 * i + 3]};
        }
        for (int i = 16; i < 80; ++i) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t t = rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl(b, 30);
            b = a;
            a = t;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    };

    size_t whole = data.size() / 64 * 64;
    for (size_t i = 0; i < whole; i += 64) process(data.data() + i);

    // The tail, a 1 bit, zeros, and the length in bits
    std::array<uint8_t, 128> tail{};
    size_t rest = data.size() - whole;
    for (size_t i = 0; i < rest; ++i) tail[i] = data[whole + i];
    tail[rest] = 0x80;
    size_t tail_length = (rest + 1 + 8 <= 64) ? 64 : 128;
    uint64_t bits = static_cast<uint64_t>(data.size()) * 8;
    for (int i = 0; i < 8; ++i) {
        tail[tail_length - 1 - static_cast<size_t>(i)] = static_cast<uint8_t>(bits >> (8 * i));
    }
    for (size_t i = 0; i < tail_length; i += 64) process(tail.data() + i);

    std::array<uint8_t, 20> digest{};
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 4; ++j) {
            digest[static_cast<size_t>(4 * i + j)] = static_cast<uint8_t>(h[i] >> (24 - 8 * j));
        }
    }
    return digest;
}

// The SHA-1 of the data as 40 lower-case hex digits
inline std::string sha1_hex(std::span<const uint8_t> data) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string hex;
    for (uint8_t byte : sha1(data)) {
        hex += digits[byte >> 4];
        hex += digits[byte & 0x0F];
    }
    return hex;
}

} // namespace beebium
