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

// A latency benchmark for the screen-text reader, for callers that need to
// budget for a copy.
//
// Both searches are the shipped `screentext::read`, timed directly: the
// aligned grid walk and the off-grid pass. Each is given a whole screen
// filled with matchable text, which is the most work either can be handed --
// a blank or purely graphical screen is cheaper, not dearer -- and the
// off-grid pass is pushed further by a screen of colour noise, its ceiling.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "screentext/ScreenText.hpp"

namespace {

using Clock = std::chrono::steady_clock;

// Best of several runs: the minimum is the truest picture of the compute,
// carrying the least scheduling noise.
template <typename Fn>
double best_ms(int runs, Fn&& fn)
{
    double best = 1e30;
    for (int i = 0; i < runs; ++i) {
        const auto start = Clock::now();
        fn();
        const std::chrono::duration<double, std::milli> elapsed
            = Clock::now() - start;
        best = elapsed.count() < best ? elapsed.count() : best;
    }
    return best;
}

const screentext::GlyphSet& acorn()
{
    return screentext::builtin_glyph_set("acorn-mos-1.20");
}

// Fill an image with the printable cycle, one glyph per cell, so every cell is
// matchable. `colours` distinct foregrounds are used in turn, on a background
// of zero, to stand in for a mode's colour depth.
screentext::Image testcard(std::size_t width, std::size_t height,
                           unsigned colours)
{
    screentext::Image image;
    image.width = width;
    image.height = height;
    image.pixels.assign(width * height, 0);

    const screentext::GlyphSet& set = acorn();
    std::size_t index = 0;
    for (std::size_t cy = 0; cy + 8 <= height; cy += 8) {
        for (std::size_t cx = 0; cx + 8 <= width; cx += 8) {
            const screentext::Glyph& glyph = set.glyphs[index % set.glyphs.size()];
            const std::uint8_t fg
                = static_cast<std::uint8_t>(1 + index % (colours ? colours : 1));
            for (std::size_t y = 0; y < 8; ++y) {
                for (std::size_t x = 0; x < 8; ++x) {
                    if (glyph.bitmap.pixel(x, y)) {
                        image.pixels[(cy + y) * width + cx + x] = fg;
                    }
                }
            }
            ++index;
        }
    }
    return image;
}


struct Mode {
    const char* name;
    std::size_t width;
    std::size_t height;
    unsigned colours;   // logical colours the mode allows
};

} // namespace

int main()
{
    // Every BBC bitmap mode is 8 logical pixels per column; the modes differ in
    // width and colour depth. MODE 0 is the largest, MODE 2 the deepest.
    const Mode modes[] = {
        {"MODE 0 (640x256, 2 colour)", 640, 256, 2},
        {"MODE 1 (320x256, 4 colour)", 320, 256, 4},
        {"MODE 2 (160x256, 16 colour)", 160, 256, 16},
        {"MODE 5 (160x256, 4 colour)", 160, 256, 4},
    };

    const std::vector<screentext::GlyphSet> sets = {acorn()};

    std::printf("%-30s %8s %10s %12s\n", "mode", "cells", "aligned", "offset");
    std::printf("%-30s %8s %10s %12s\n", "", "", "ms", "ms");
    std::printf("%s\n", std::string(62, '-').c_str());

    for (const Mode& mode : modes) {
        const screentext::Image image
            = testcard(mode.width, mode.height, mode.colours);

        screentext::Band band;
        band.bottom = mode.height;
        band.cell_width = 8;
        band.cell_height = 8;

        screentext::Options offset_options;
        offset_options.search = screentext::Search::OffsetOnly;

        std::size_t total_cells = 0;
        const double aligned = best_ms(20, [&] {
            const screentext::Result result
                = screentext::read(image, {band}, sets);
            total_cells = result.total_cells;
        });

        const double offset = best_ms(5, [&] {
            screentext::read(image, {band}, sets, offset_options);
        });

        std::printf("%-30s %8zu %10.2f %12.1f\n", mode.name, total_cells,
                    aligned, offset);
    }

    // The offset ceiling: a full MODE 0 screen of eight-colour noise, so that
    // almost every window carries the maximum colours and the per-position
    // work is at its worst. No real screen looks like this; it is the number
    // beyond which the search cannot go at this size.
    {
        screentext::Image noise;
        noise.width = 640;
        noise.height = 256;
        noise.pixels.resize(noise.width * noise.height);
        std::uint32_t state = 0x1234567u;
        for (std::uint8_t& pixel : noise.pixels) {
            state = state * 1664525u + 1013904223u;
            pixel = static_cast<std::uint8_t>((state >> 24) & 0x07u);
        }
        screentext::Band band;
        band.bottom = noise.height;
        band.cell_width = 8;
        band.cell_height = 8;
        screentext::Options offset_options;
        offset_options.search = screentext::Search::OffsetOnly;
        const double offset = best_ms(5, [&] {
            screentext::read(noise, {band}, sets, offset_options);
        });
        std::printf("%-30s %8s %10s %12.1f\n",
                    "MODE 0 8-colour noise (ceiling)", "-", "-", offset);
    }

    std::printf("\naligned: shipped screentext::read, Search::AlignedOnly.\n");
    std::printf("offset:  shipped screentext::read, Search::OffsetOnly.\n");
    std::printf("Both worst case: every cell filled with matchable text.\n");
    return 0;
}
