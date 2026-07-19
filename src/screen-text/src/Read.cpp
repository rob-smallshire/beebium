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

#include "screentext/Read.hpp"

#include <stdexcept>

#include "GlyphIndex.hpp"

namespace screentext {

namespace {

// Append a codepoint to a UTF-8 string.
void append_utf8(std::string& text, char32_t codepoint)
{
    if (codepoint < 0x80U) {
        text.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800U) {
        text.push_back(static_cast<char>(0xC0U | (codepoint >> 6)));
        text.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    } else if (codepoint < 0x10000U) {
        text.push_back(static_cast<char>(0xE0U | (codepoint >> 12)));
        text.push_back(static_cast<char>(0x80U | ((codepoint >> 6) & 0x3FU)));
        text.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    } else {
        text.push_back(static_cast<char>(0xF0U | (codepoint >> 18)));
        text.push_back(static_cast<char>(0x80U | ((codepoint >> 12) & 0x3FU)));
        text.push_back(static_cast<char>(0x80U | ((codepoint >> 6) & 0x3FU)));
        text.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    }
}

// Reduce a cell of the image to one bit per pixel: a pixel equal to the
// band's background is clear, anything else is set. This is what removes
// colour from the problem, so the same glyph in any colour is one bitmap.
Bitmap extract_cell(const Image& image, const Rect& bounds, std::uint8_t background)
{
    Bitmap bitmap(bounds.width, bounds.height);
    for (std::size_t y = 0; y < bounds.height; ++y) {
        for (std::size_t x = 0; x < bounds.width; ++x) {
            if (image.pixel(bounds.x + x, bounds.y + y) != background) {
                bitmap.set_pixel(x, y, true);
            }
        }
    }
    return bitmap;
}

void validate(const Image& image, const std::vector<Band>& bands)
{
    if (!image.consistent()) {
        throw std::invalid_argument(
            "screentext::read: image pixel count does not match its dimensions");
    }
    for (const Band& band : bands) {
        if (band.cell_width == 0 || band.cell_height == 0) {
            throw std::invalid_argument(
                "screentext::read: band cell size must not be zero");
        }
        // A pitch below the cell size would make cells overlap, which no grid
        // does. Refusing it is better than silently reading pixels twice.
        if (band.effective_column_pitch() < band.cell_width
            || band.effective_row_pitch() < band.cell_height) {
            throw std::invalid_argument(
                "screentext::read: band pitch must be at least the cell size");
        }
    }
}

// The region cells may be taken from: the selection where given, clipped to
// the image, and to the band.
bool region_for(const Image& image,
                const Band& band,
                const std::optional<Rect>& selection,
                Rect& region)
{
    std::size_t left = 0;
    std::size_t top = band.top;
    std::size_t right = image.width;
    std::size_t bottom = band.bottom < image.height ? band.bottom : image.height;

    if (selection.has_value()) {
        const Rect& rect = *selection;
        left = rect.x > left ? rect.x : left;
        top = rect.y > top ? rect.y : top;
        right = rect.right() < right ? rect.right() : right;
        bottom = rect.bottom() < bottom ? rect.bottom() : bottom;
    }

    if (left >= right || top >= bottom) {
        return false;
    }
    region = Rect{left, top, right - left, bottom - top};
    return true;
}

// Finish a run: trim the blank cells from each end and record it, unless
// there is nothing worth reporting. A cell is worth reporting when it matched
// something other than a blank glyph, or when it matched nothing at all --
// the latter being the difference between "unreadable" and "empty".
void flush_run(std::vector<Cell>& cells, std::vector<Run>& runs)
{
    const auto interesting = [](const Cell& cell) {
        return !cell.matched() || cell.codepoint != U' ';
    };

    std::size_t first = 0;
    while (first < cells.size() && !interesting(cells[first])) {
        ++first;
    }
    if (first == cells.size()) {
        cells.clear();
        return;
    }
    std::size_t last = cells.size();
    while (last > first && !interesting(cells[last - 1])) {
        --last;
    }

    Run run;
    run.cells.assign(cells.begin() + static_cast<std::ptrdiff_t>(first),
                     cells.begin() + static_cast<std::ptrdiff_t>(last));

    for (const Cell& cell : run.cells) {
        // An unmatched cell contributes a space, so that columns stay where
        // they were on screen. That it was unreadable rather than blank is
        // recorded on the cell, never lost.
        append_utf8(run.text, cell.matched() ? cell.codepoint : U' ');
    }

    const Rect& front = run.cells.front().bounds;
    const Rect& back = run.cells.back().bounds;
    run.bounds = Rect{front.x, front.y, back.right() - front.x, front.height};

    runs.push_back(std::move(run));
    cells.clear();
}

} // namespace

std::size_t Run::unmatched_cells() const
{
    std::size_t count = 0;
    for (const Cell& cell : cells) {
        if (!cell.matched()) {
            ++count;
        }
    }
    return count;
}

std::string Result::text() const
{
    std::string text;
    for (std::size_t index = 0; index < runs.size(); ++index) {
        if (index > 0) {
            text.push_back('\n');
        }
        text += runs[index].text;
    }
    return text;
}

Result read(const Image& image,
            const std::vector<Band>& bands,
            const std::vector<GlyphSet>& glyph_sets,
            const Options& options)
{
    validate(image, bands);

    // Search::IncludeOffset is accepted so that callers and the wire format
    // can be written against the finished interface, but sub-cell offset
    // search is not implemented: reading is aligned either way, and no cell
    // is ever reported as having matched at an offset.

    const GlyphIndex index(glyph_sets, options.match_inverted);

    Result result;
    if (image.empty()) {
        return result;
    }

    // Bands in the order given, so reading order is the caller's to decide.
    for (const Band& band : bands) {
        Rect region;
        if (!region_for(image, band, options.selection, region)) {
            continue;
        }

        // Walk the grid from its origin, advancing by the pitch but reading
        // only the cell. Where the two differ the space between cells is
        // stepped over without ever being sampled. Cells before the region
        // are skipped rather than shifted, so the grid stays where the caller
        // put it.
        const std::size_t column_pitch = band.effective_column_pitch();
        const std::size_t row_pitch = band.effective_row_pitch();

        std::vector<Cell> row_cells;
        for (std::size_t y = band.origin_y;
             y + band.cell_height <= region.bottom();
             y += row_pitch) {
            if (y < region.y) {
                continue;
            }

            row_cells.clear();
            for (std::size_t x = band.origin_x;
                 x + band.cell_width <= region.right();
                 x += column_pitch) {
                if (x < region.x) {
                    continue;
                }

                const Rect bounds{x, y, band.cell_width, band.cell_height};

                Cell cell;
                cell.bounds = bounds;

                const Bitmap bitmap
                    = extract_cell(image, bounds, band.background);
                if (const GlyphIndex::Match* match = index.find(bitmap)) {
                    cell.codepoint = match->codepoint;
                    cell.inverted = match->inverted;
                    cell.glyph_set = *match->glyph_set;
                } else {
                    ++result.unmatched_cells;
                }

                ++result.total_cells;
                row_cells.push_back(std::move(cell));
            }

            flush_run(row_cells, result.runs);
        }
    }

    return result;
}

} // namespace screentext
