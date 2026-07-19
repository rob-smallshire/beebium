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

// What a cell reduced to: the bitmap, and which value was taken as its
// background so that inverse video can be judged afterwards.
struct ReducedCell {
    Bitmap bitmap;
    std::uint8_t background = 0;
    bool usable = false; // false when the cell cannot be one glyph
};

// Reduce a cell to one bit per pixel against a known background: a pixel
// equal to it is clear, anything else is set.
ReducedCell reduce_against(const Image& image,
                           const Rect& bounds,
                           std::uint8_t background)
{
    ReducedCell reduced;
    reduced.bitmap = Bitmap(bounds.width, bounds.height);
    reduced.background = background;
    reduced.usable = true;

    for (std::size_t y = 0; y < bounds.height; ++y) {
        for (std::size_t x = 0; x < bounds.width; ++x) {
            if (image.pixel(bounds.x + x, bounds.y + y) != background) {
                reduced.bitmap.set_pixel(x, y, true);
            }
        }
    }
    return reduced;
}

// Reduce a cell without being told which value is background.
//
// A character cell drawn by the VDU drivers holds exactly two values, so the
// pair can be recovered from the cell itself. Which of the two is background
// need not be decided correctly for matching -- the two assignments give a
// bitmap and its complement, and both are looked up -- but it is decided
// deterministically anyway, preferring the value the rest of the screen uses,
// so that inverse video can be told apart from ordinary text.
//
// Three or more values means the cell is not one glyph in one colour on one
// background. It is left unusable rather than guessed at.
ReducedCell reduce_by_inspection(const Image& image,
                                 const Rect& bounds,
                                 std::uint8_t screen_background)
{
    ReducedCell reduced;

    std::uint8_t values[2] = {0, 0};
    std::size_t counts[2] = {0, 0};
    std::size_t distinct = 0;

    for (std::size_t y = 0; y < bounds.height; ++y) {
        for (std::size_t x = 0; x < bounds.width; ++x) {
            const std::uint8_t value = image.pixel(bounds.x + x, bounds.y + y);
            if (distinct > 0 && value == values[0]) {
                ++counts[0];
            } else if (distinct > 1 && value == values[1]) {
                ++counts[1];
            } else if (distinct < 2) {
                values[distinct] = value;
                counts[distinct] = 1;
                ++distinct;
            } else {
                return reduced; // a third colour: not one glyph
            }
        }
    }

    if (distinct == 0) {
        return reduced;
    }

    // A cell of one value is blank, whatever that value is.
    if (distinct == 1) {
        reduced.bitmap = Bitmap(bounds.width, bounds.height);
        reduced.background = values[0];
        reduced.usable = true;
        return reduced;
    }

    // Prefer whichever value the screen uses as its background; failing that,
    // the one covering more of the cell, with the lower value breaking a tie
    // so that the result never depends on scan order.
    std::uint8_t background = 0;
    if (values[0] == screen_background) {
        background = values[0];
    } else if (values[1] == screen_background) {
        background = values[1];
    } else if (counts[0] != counts[1]) {
        background = counts[0] > counts[1] ? values[0] : values[1];
    } else {
        background = values[0] < values[1] ? values[0] : values[1];
    }

    return reduce_against(image, bounds, background);
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

// Which value a single cell would call its background, judged only by what is
// in the cell: the one covering more of it, with the lower value breaking a
// tie. A cell of three or more values is not one glyph and has no opinion.
std::optional<std::uint8_t> cell_background(const Image& image,
                                            const Rect& bounds)
{
    std::uint8_t values[2] = {0, 0};
    std::size_t counts[2] = {0, 0};
    std::size_t distinct = 0;

    for (std::size_t y = 0; y < bounds.height; ++y) {
        for (std::size_t x = 0; x < bounds.width; ++x) {
            const std::uint8_t value = image.pixel(bounds.x + x, bounds.y + y);
            if (distinct > 0 && value == values[0]) {
                ++counts[0];
            } else if (distinct > 1 && value == values[1]) {
                ++counts[1];
            } else if (distinct < 2) {
                values[distinct] = value;
                counts[distinct] = 1;
                ++distinct;
            } else {
                return std::nullopt;
            }
        }
    }

    if (distinct == 0) {
        return std::nullopt;
    }
    if (distinct == 1) {
        return values[0];
    }
    if (counts[0] != counts[1]) {
        return counts[0] > counts[1] ? values[0] : values[1];
    }
    return values[0] < values[1] ? values[0] : values[1];
}

// The value the band uses as its background, by a vote of one per cell.
//
// This is what makes inverse video detectable without being told. Both
// readings of a two-colour cell are glyphs, so which one is "inverse" is not
// a property of the cell at all -- it is how the cell sits against the rest
// of the screen, and that can be measured.
//
// Cells vote rather than pixels. Counting pixels lets a handful of solid
// cells outweigh a screenful of text, because a solid cell contributes every
// one of its pixels while a letter contributes only its strokes; one cell of
// reverse video in four was enough to invert the answer. One cell, one vote
// is far harder to skew, and matches what "the background of this screen"
// means to somebody looking at it.
std::uint8_t infer_background(const Image& image,
                              const std::vector<std::vector<Rect>>& rows)
{
    std::size_t votes[256] = {};
    for (const std::vector<Rect>& row : rows) {
        for (const Rect& bounds : row) {
            if (const std::optional<std::uint8_t> value
                = cell_background(image, bounds)) {
                ++votes[*value];
            }
        }
    }

    std::size_t best = 0;
    for (std::size_t value = 1; value < 256; ++value) {
        if (votes[value] > votes[best]) {
            best = value;
        }
    }
    return static_cast<std::uint8_t>(best);
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

        const std::vector<std::vector<Rect>> rows = cells_of(band, region);

        // One value stands for the band's background, and it is measured
        // rather than declared. It decides only what reads as inverse video:
        // the text comes out the same either way, because both readings of a
        // cell's two colours are tried.
        const std::uint8_t screen_background = infer_background(image, rows);

        std::vector<Cell> row_cells;
        for (const std::vector<Rect>& row : rows) {
            row_cells.clear();
            for (const Rect& bounds : row) {
                Cell cell;
                cell.bounds = bounds;

                const ReducedCell reduced
                    = reduce_by_inspection(image, bounds, screen_background);

                const GlyphIndex::Match* match
                    = reduced.usable ? index.find(reduced.bitmap) : nullptr;
                if (match != nullptr) {
                    cell.codepoint = match->codepoint;
                    cell.glyph_set = *match->glyph_set;

                    // Two things can make a cell inverse: the glyph matched
                    // as a complement, or the cell's background is not the
                    // screen's. Either alone means inverse; both together
                    // cancel, the cell having been read the other way up
                    // already.
                    const bool reversed_ground
                        = reduced.background != screen_background;
                    cell.inverted = match->inverted != reversed_ground;
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
