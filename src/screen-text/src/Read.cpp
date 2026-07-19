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

#include <algorithm>
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

// ---- off-grid search ----------------------------------------------------
//
// VDU 5 draws text at the graphics cursor, at any pixel position, over
// whatever is already there. The grid walk above misses all of it. This finds
// it, at the cost of sixty-four sub-cell offsets times the colours in each
// window; it is a separate pass, selected by Search::IncludeOffset, and the
// aligned path never runs any of it.

// One glyph found at a pixel position, in one colour.
struct OffsetCandidate {
    Rect bounds;
    std::uint8_t foreground = 0;
    const GlyphIndex::Match* match = nullptr;
};

// Set a bit in `bitmap` where the pixel is the glyph colour; everything else
// is background, whatever colours it holds. This is "do the pixels of colour c
// form a glyph, ignoring everything else" -- the question VDU 5 text needs,
// having been painted over arbitrary graphics with no background of its own.
//
// The bitmap is cleared and refilled rather than allocated afresh, so one
// scratch bitmap serves the whole search. The gather visits every pixel of the
// region once per colour present, and an allocation per lookup there would
// dominate the cost.
void reduce_foreground(const Image& image, const Rect& bounds,
                       std::uint8_t foreground, Bitmap& bitmap)
{
    bitmap.clear();
    for (std::size_t y = 0; y < bounds.height; ++y) {
        for (std::size_t x = 0; x < bounds.width; ++x) {
            if (image.pixel(bounds.x + x, bounds.y + y) == foreground) {
                bitmap.set_pixel(x, y, true);
            }
        }
    }
}

// The colour covering most of a window other than the glyph's: a best-effort
// "what it sits on". Off-grid text has no single background, so this is the
// dominant surrounding colour rather than a second ink. Stays the foreground
// when the window holds nothing else.
std::uint8_t dominant_other(const Image& image, const Rect& bounds,
                            std::uint8_t foreground)
{
    std::size_t counts[256] = {};
    for (std::size_t y = 0; y < bounds.height; ++y) {
        for (std::size_t x = 0; x < bounds.width; ++x) {
            ++counts[image.pixel(bounds.x + x, bounds.y + y)];
        }
    }
    std::size_t best = 0;
    std::uint8_t value = foreground;
    for (std::size_t c = 0; c < 256; ++c) {
        if (c != foreground && counts[c] > best) {
            best = counts[c];
            value = static_cast<std::uint8_t>(c);
        }
    }
    return value;
}

// Every glyph-shaped patch of one colour, at every position in the region.
std::vector<OffsetCandidate> gather_offset(const Image& image,
                                           const Rect& region,
                                           std::size_t cell_width,
                                           std::size_t cell_height,
                                           const GlyphIndex& index)
{
    std::vector<OffsetCandidate> candidates;
    if (region.width < cell_width || region.height < cell_height) {
        return candidates;
    }
    const std::size_t last_x = region.right() - cell_width;
    const std::size_t last_y = region.bottom() - cell_height;

    // One scratch bitmap for the whole search, refilled per lookup.
    Bitmap scratch(cell_width, cell_height);

    for (std::size_t y = region.y; y <= last_y; ++y) {
        for (std::size_t x = region.x; x <= last_x; ++x) {
            const Rect bounds{x, y, cell_width, cell_height};

            // The colours present in this window, gathered once. Only these
            // are tried, so a colour that is not here -- and in particular an
            // empty bitmap, which would be a space -- can never match.
            std::uint8_t colours[64];
            std::size_t colour_count = 0;
            for (std::size_t dy = 0; dy < cell_height; ++dy) {
                for (std::size_t dx = 0; dx < cell_width; ++dx) {
                    const std::uint8_t value = image.pixel(x + dx, y + dy);
                    bool known = false;
                    for (std::size_t k = 0; k < colour_count; ++k) {
                        if (colours[k] == value) { known = true; break; }
                    }
                    if (!known && colour_count < 64) {
                        colours[colour_count++] = value;
                    }
                }
            }

            // A window of one colour has no glyph in it: reducing to that
            // colour sets every pixel, the solid block, which is not in any
            // set. So the flat regions a picture is mostly made of are skipped
            // for the price of the colour scan alone.
            if (colour_count < 2) {
                continue;
            }

            for (std::size_t k = 0; k < colour_count; ++k) {
                reduce_foreground(image, bounds, colours[k], scratch);
                if (const GlyphIndex::Match* match = index.find(scratch)) {
                    candidates.push_back({bounds, colours[k], match});
                }
            }
        }
    }
    return candidates;
}

// Whether two steps count as one lattice step: eight give or take one, since a
// game placing characters by hand does not always land on a perfect grid.
bool one_step(std::size_t gap, std::size_t cell_width)
{
    return gap + 1 >= cell_width && gap <= cell_width + 1;
}

// Group candidates into runs: same baseline, same colour, one lattice step
// apart. Text is regular where noise is not, which is what tells them apart.
std::vector<std::vector<OffsetCandidate>> register_runs(
    std::vector<OffsetCandidate> candidates, std::size_t cell_width)
{
    std::sort(candidates.begin(), candidates.end(),
              [](const OffsetCandidate& a, const OffsetCandidate& b) {
                  if (a.bounds.y != b.bounds.y) return a.bounds.y < b.bounds.y;
                  if (a.foreground != b.foreground)
                      return a.foreground < b.foreground;
                  return a.bounds.x < b.bounds.x;
              });

    std::vector<std::vector<OffsetCandidate>> runs;
    for (const OffsetCandidate& candidate : candidates) {
        if (!runs.empty()) {
            const OffsetCandidate& previous = runs.back().back();
            if (previous.bounds.y == candidate.bounds.y
                && previous.foreground == candidate.foreground) {
                const std::size_t gap = candidate.bounds.x - previous.bounds.x;
                if (gap == 0) {
                    continue; // the same position and colour, already taken
                }
                if (one_step(gap, cell_width)) {
                    runs.back().push_back(candidate);
                    continue;
                }
            }
        }
        runs.push_back({candidate});
    }
    return runs;
}

// A run is credible when it holds at least one distinctive glyph: something
// imagery does not draw by accident. The simple glyphs in it come along, being
// vouched for by the company they keep.
bool credible(const std::vector<OffsetCandidate>& run)
{
    for (const OffsetCandidate& candidate : run) {
        if (candidate.match->distinctive) {
            return true;
        }
    }
    return false;
}

// Keep the longest runs and drop any that overlap one already kept: the same
// pixels cannot be two characters. This is what removes the ghosts real text
// casts at near-miss offsets. Deterministic: longest first, then top to
// bottom, then left to right.
std::vector<std::vector<OffsetCandidate>> resolve_overlaps(
    std::vector<std::vector<OffsetCandidate>> runs, const Image& image)
{
    std::sort(runs.begin(), runs.end(),
              [](const std::vector<OffsetCandidate>& a,
                 const std::vector<OffsetCandidate>& b) {
                  if (a.size() != b.size()) return a.size() > b.size();
                  if (a.front().bounds.y != b.front().bounds.y)
                      return a.front().bounds.y < b.front().bounds.y;
                  return a.front().bounds.x < b.front().bounds.x;
              });

    std::vector<char> claimed(image.width * image.height, 0);
    const auto overlaps = [&](const Rect& r) {
        for (std::size_t y = 0; y < r.height; ++y) {
            for (std::size_t x = 0; x < r.width; ++x) {
                if (claimed[(r.y + y) * image.width + r.x + x]) {
                    return true;
                }
            }
        }
        return false;
    };
    const auto claim = [&](const Rect& r) {
        for (std::size_t y = 0; y < r.height; ++y) {
            for (std::size_t x = 0; x < r.width; ++x) {
                claimed[(r.y + y) * image.width + r.x + x] = 1;
            }
        }
    };

    std::vector<std::vector<OffsetCandidate>> kept;
    for (const std::vector<OffsetCandidate>& run : runs) {
        bool clash = false;
        for (const OffsetCandidate& candidate : run) {
            if (overlaps(candidate.bounds)) { clash = true; break; }
        }
        if (clash) {
            continue;
        }
        for (const OffsetCandidate& candidate : run) {
            claim(candidate.bounds);
        }
        kept.push_back(run);
    }
    return kept;
}

// Build a Run from a sequence of candidates on one baseline in one colour,
// writing spaces back where the lattice shows a whole-cell gap between them.
// SCORE 1000 arrives as two candidate sequences a couple of cells apart; this
// is where the space between them is restored.
Run build_offset_run(const std::vector<OffsetCandidate>& run,
                     const Image& image, std::size_t cell_width)
{
    Run out;
    for (std::size_t i = 0; i < run.size(); ++i) {
        const OffsetCandidate& candidate = run[i];

        if (i > 0) {
            // Fill any whole-cell gap since the previous glyph with spaces.
            const std::size_t span
                = candidate.bounds.x - run[i - 1].bounds.x;
            const std::size_t steps
                = (span + cell_width / 2) / cell_width; // nearest whole cells
            for (std::size_t s = 1; s < steps; ++s) {
                Cell space;
                space.codepoint = U' ';
                space.offset = true;
                const std::size_t sx = run[i - 1].bounds.x + s * cell_width;
                space.bounds = Rect{sx, candidate.bounds.y, cell_width,
                                    candidate.bounds.height};
                out.cells.push_back(space);
            }
        }

        Cell cell;
        cell.bounds = candidate.bounds;
        cell.codepoint = candidate.match->codepoint;
        cell.glyph_set = *candidate.match->glyph_set;
        cell.alternatives = candidate.match->alternatives;
        cell.foreground = candidate.foreground;
        cell.background = dominant_other(image, candidate.bounds,
                                         candidate.foreground);
        cell.offset = true;
        out.cells.push_back(std::move(cell));
    }

    for (const Cell& cell : out.cells) {
        append_utf8(out.text, cell.codepoint);
    }
    const Rect& front = out.cells.front().bounds;
    const Rect& back = out.cells.back().bounds;
    out.bounds = Rect{front.x, front.y, back.right() - front.x, front.height};
    return out;
}

// Rejoin runs across a space: same baseline, same colour, a whole number of
// lattice steps apart. Two kept runs separated by one cell become one run with
// a space between them.
std::vector<std::vector<OffsetCandidate>> rejoin_across_spaces(
    std::vector<std::vector<OffsetCandidate>> runs, std::size_t cell_width)
{
    std::sort(runs.begin(), runs.end(),
              [](const std::vector<OffsetCandidate>& a,
                 const std::vector<OffsetCandidate>& b) {
                  if (a.front().bounds.y != b.front().bounds.y)
                      return a.front().bounds.y < b.front().bounds.y;
                  if (a.front().foreground != b.front().foreground)
                      return a.front().foreground < b.front().foreground;
                  return a.front().bounds.x < b.front().bounds.x;
              });

    std::vector<std::vector<OffsetCandidate>> merged;
    for (std::vector<OffsetCandidate>& run : runs) {
        if (!merged.empty()) {
            std::vector<OffsetCandidate>& into = merged.back();
            const OffsetCandidate& tail = into.back();
            const OffsetCandidate& head = run.front();
            if (tail.bounds.y == head.bounds.y
                && tail.foreground == head.foreground
                && head.bounds.x > tail.bounds.x) {
                const std::size_t span = head.bounds.x - tail.bounds.x;
                const std::size_t steps = (span + cell_width / 2) / cell_width;
                // A whole number of steps, at least two (one step would have
                // been chained already), landing near a lattice point.
                const std::size_t ideal = steps * cell_width;
                const std::size_t drift
                    = span > ideal ? span - ideal : ideal - span;
                if (steps >= 2 && drift <= 1) {
                    into.insert(into.end(), run.begin(), run.end());
                    continue;
                }
            }
        }
        merged.push_back(std::move(run));
    }
    return merged;
}

// Whether a run sits on the band's character grid, in which case the aligned
// pass owns it and the off-grid pass leaves it alone. The two passes then
// partition the text between them.
bool on_grid(const std::vector<OffsetCandidate>& run, const Band& band)
{
    const OffsetCandidate& first = run.front();
    const std::size_t row_pitch = band.effective_row_pitch();
    const std::size_t column_pitch = band.effective_column_pitch();
    const bool row_aligned = first.bounds.y >= band.origin_y
        && (first.bounds.y - band.origin_y) % row_pitch == 0;
    const bool column_aligned = first.bounds.x >= band.origin_x
        && (first.bounds.x - band.origin_x) % column_pitch == 0;
    return row_aligned && column_aligned;
}

void read_offset(const Image& image, const std::vector<Band>& bands,
                 const GlyphIndex& index, const Options& options,
                 Result& result)
{
    for (const Band& band : bands) {
        Rect region;
        if (!region_for(image, band, options.selection, region)) {
            continue;
        }

        std::vector<std::vector<OffsetCandidate>> runs = register_runs(
            gather_offset(image, region, band.cell_width, band.cell_height,
                          index),
            band.cell_width);

        std::vector<std::vector<OffsetCandidate>> good;
        for (std::vector<OffsetCandidate>& run : runs) {
            if (credible(run)) {
                good.push_back(std::move(run));
            }
        }

        // Overlap resolution runs first, with grid runs still present: real
        // grid text claims its pixels and so suppresses the near-miss ghosts
        // that real text casts at neighbouring offsets. Only then are grid
        // runs dropped -- the aligned pass owns them -- so that rejoining
        // across spaces cannot fuse an off-grid label onto a grid run that
        // happens to share its baseline and colour.
        good = resolve_overlaps(std::move(good), image);

        std::vector<std::vector<OffsetCandidate>> off_grid;
        for (std::vector<OffsetCandidate>& run : good) {
            if (!on_grid(run, band)) {
                off_grid.push_back(std::move(run));
            }
        }

        good = rejoin_across_spaces(std::move(off_grid), band.cell_width);

        std::sort(good.begin(), good.end(),
                  [](const std::vector<OffsetCandidate>& a,
                     const std::vector<OffsetCandidate>& b) {
                      if (a.front().bounds.y != b.front().bounds.y)
                          return a.front().bounds.y < b.front().bounds.y;
                      return a.front().bounds.x < b.front().bounds.x;
                  });

        for (const std::vector<OffsetCandidate>& run : good) {
            Run built = build_offset_run(run, image, band.cell_width);
            for (const Cell& cell : built.cells) {
                ++result.total_cells;
                if (cell.ambiguous()) {
                    ++result.ambiguous_cells;
                }
            }
            result.runs.push_back(std::move(built));
        }
    }
}

// The aligned grid walk: the whole of the first increment, unchanged.
void read_aligned(const Image& image, const std::vector<Band>& bands,
                  const GlyphIndex& index, const Options& options,
                  Result& result)
{
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
                    cell.alternatives = match->alternatives;
                    cell.background = background;
                    if (!match->alternatives.empty()) {
                        ++result.ambiguous_cells;
                    }

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

std::size_t Run::ambiguous_cells() const
{
    std::size_t count = 0;
    for (const Cell& cell : cells) {
        if (cell.ambiguous()) {
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

    const GlyphIndex index(glyph_sets);

    Result result;
    if (image.empty()) {
        return result;
    }

    // One pass or the other, never both: a caller wanting aligned and off-grid
    // text runs read() twice and concatenates. The two searches partition the
    // text -- aligned reads what sits on the grid, off-grid reads the rest --
    // so there is nothing to deduplicate, and Cell::offset says which pass a
    // run came from: false throughout an aligned result, true throughout an
    // off-grid one.
    if (options.search == Search::IncludeOffset) {
        read_offset(image, bands, index, options, result);
    } else {
        read_aligned(image, bands, index, options, result);
    }

    return result;
}

} // namespace screentext
