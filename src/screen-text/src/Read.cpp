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

#include <optional>
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

// The two colours a cell is drawn from, and how many pixels each covers.
struct CellColours {
    std::uint8_t values[2] = {0, 0};
    std::size_t counts[2] = {0, 0};
    std::size_t distinct = 0;
};

// Collect a cell's colours. A character cell drawn by the VDU drivers holds
// exactly two: the glyph's and its background's. Three or more means the cell
// is not one glyph in one colour on one background, and it is left for the
// caller to reject rather than guessed at.
CellColours colours_of(const Image& image, const Rect& bounds)
{
    CellColours colours;
    for (std::size_t y = 0; y < bounds.height; ++y) {
        for (std::size_t x = 0; x < bounds.width; ++x) {
            const std::uint8_t value = image.pixel(bounds.x + x, bounds.y + y);
            if (colours.distinct > 0 && value == colours.values[0]) {
                ++colours.counts[0];
            } else if (colours.distinct > 1 && value == colours.values[1]) {
                ++colours.counts[1];
            } else if (colours.distinct < 2) {
                colours.values[colours.distinct] = value;
                colours.counts[colours.distinct] = 1;
                ++colours.distinct;
            } else {
                colours.distinct = 3;
                return colours;
            }
        }
    }
    return colours;
}

// Reduce a cell to one bit per pixel against a chosen background: a pixel
// equal to it is clear, anything else is set.
Bitmap reduce_against(const Image& image,
                      const Rect& bounds,
                      std::uint8_t background)
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
// The cell rectangles of a band, row by row, in reading order.
std::vector<std::vector<Rect>> cells_of(const Band& band, const Rect& region)
{
    const std::size_t column_pitch = band.effective_column_pitch();
    const std::size_t row_pitch = band.effective_row_pitch();

    // Walk the grid from its origin, advancing by the pitch but taking only
    // the cell. Where the two differ the space between cells is stepped over
    // and never sampled. Cells before the region are skipped rather than
    // shifted, so the grid stays where the caller put it.
    std::vector<std::vector<Rect>> rows;
    for (std::size_t y = band.origin_y;
         y + band.cell_height <= region.bottom();
         y += row_pitch) {
        if (y < region.y) {
            continue;
        }

        std::vector<Rect> row;
        for (std::size_t x = band.origin_x;
             x + band.cell_width <= region.right();
             x += column_pitch) {
            if (x < region.x) {
                continue;
            }
            row.push_back(Rect{x, y, band.cell_width, band.cell_height});
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

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

    const GlyphIndex index(glyph_sets);

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

        const std::vector<std::vector<Rect>> rows = cells_of(band, region);

        std::vector<Cell> row_cells;
        for (const std::vector<Rect>& row : rows) {
            row_cells.clear();
            for (const Rect& bounds : row) {
                Cell cell;
                cell.bounds = bounds;

                // Try the cell both ways round. Which way matches says which
                // colour the glyph was drawn in, so nothing needs to assume
                // an orientation, and nothing needs to know what the rest of
                // the screen is doing.
                const CellColours colours = colours_of(image, bounds);
                const GlyphIndex::Match* match = nullptr;
                std::uint8_t background = 0;

                for (std::size_t choice = 0; choice < colours.distinct
                     && choice < 2; ++choice) {
                    const std::uint8_t candidate = colours.values[choice];
                    const GlyphIndex::Match* found
                        = index.find(reduce_against(image, bounds, candidate));
                    if (found == nullptr) {
                        continue;
                    }
                    if (match == nullptr) {
                        match = found;
                        background = candidate;
                        continue;
                    }

                    // Both readings are glyphs, which needs a glyph set
                    // holding some glyph's complement. Take the one whose
                    // background covers more of the cell, the lower value
                    // breaking a tie, so the answer is fixed either way.
                    const std::size_t held = colours.counts[choice == 0 ? 1 : 0];
                    const std::size_t offered = colours.counts[choice];
                    if (offered > held
                        || (offered == held && candidate < background)) {
                        match = found;
                        background = candidate;
                    }
                }

                if (match != nullptr) {
                    cell.codepoint = match->codepoint;
                    cell.glyph_set = *match->glyph_set;
                    cell.background = background;

                    // A blank cell has one colour and no glyph to have the
                    // other, so both are reported the same.
                    cell.foreground = colours.distinct == 2
                        ? (colours.values[0] == background ? colours.values[1]
                                                           : colours.values[0])
                        : background;
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
