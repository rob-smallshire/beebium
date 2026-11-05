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

#include "connection/ScreenSelection.hpp"

#include <algorithm>
#include <cmath>

namespace beebium::linux_client {

namespace {

std::uint32_t non_negative(long value) {
    return static_cast<std::uint32_t>(std::max(0L, value));
}

PixelRect span(long left, long top, long right, long bottom) {
    return {non_negative(left), non_negative(top), non_negative(right - left),
            non_negative(bottom - top)};
}

GridCell cell_at(const ScreenBand& band, FramePoint point) {
    return {row_at(band, static_cast<int>(std::floor(point.y))),
            column_at(band, static_cast<int>(std::floor(point.x)))};
}

bool before(GridCell a, GridCell b) {
    return a.row != b.row ? a.row < b.row : a.column < b.column;
}

}  // namespace

bool gridded(const ScreenBand& band) {
    return band.column_pitch > 0 && band.row_pitch > 0;
}

int column_at(const ScreenBand& band, int frame_x) {
    if (band.column_pitch == 0) {
        return 0;
    }
    const int offset = frame_x - static_cast<int>(band.origin_x);
    return std::max(0, offset / static_cast<int>(band.column_pitch));
}

int row_at(const ScreenBand& band, int frame_y) {
    if (band.row_pitch == 0) {
        return 0;
    }
    const int offset = frame_y - static_cast<int>(band.origin_y);
    return std::max(0, offset / static_cast<int>(band.row_pitch));
}

int frame_x_of(const ScreenBand& band, int column) {
    return static_cast<int>(band.origin_x) + column * static_cast<int>(band.column_pitch);
}

int frame_y_of(const ScreenBand& band, int row) {
    return static_cast<int>(band.origin_y) + row * static_cast<int>(band.row_pitch);
}

const ScreenBand* selection_band(const ScreenGrid& grid, double top_y) {
    const auto y = static_cast<long>(std::floor(top_y));
    for (const ScreenBand& band : grid.bands) {
        if (y >= band.top && y < band.bottom && gridded(band)) {
            return &band;
        }
    }
    for (const ScreenBand& band : grid.bands) {
        if (gridded(band)) {
            return &band;
        }
    }
    return nullptr;
}

PixelRect bounding_rect(FramePoint a, FramePoint b) {
    return span(static_cast<long>(std::floor(std::min(a.x, b.x))),
                static_cast<long>(std::floor(std::min(a.y, b.y))),
                static_cast<long>(std::ceil(std::max(a.x, b.x))),
                static_cast<long>(std::ceil(std::max(a.y, b.y))));
}

PixelRect snapped_rectangle(FramePoint a, FramePoint b, const ScreenBand* band) {
    if (band == nullptr || !gridded(*band)) {
        return bounding_rect(a, b);
    }
    const GridCell first = cell_at(*band, {std::min(a.x, b.x), std::min(a.y, b.y)});
    const GridCell last = cell_at(*band, {std::max(a.x, b.x), std::max(a.y, b.y)});
    return span(frame_x_of(*band, first.column), frame_y_of(*band, first.row),
                frame_x_of(*band, last.column) + static_cast<long>(band->cell_width),
                frame_y_of(*band, last.row) + static_cast<long>(band->cell_height));
}

RowsFlow rows_flow(FramePoint anchor, FramePoint focus, const ScreenBand& band,
                   std::uint32_t frame_width) {
    GridCell start = cell_at(band, anchor);
    GridCell end = cell_at(band, focus);
    if (before(end, start)) {
        std::swap(start, end);
    }
    const long top = frame_y_of(band, start.row);
    const long right = std::max(static_cast<long>(frame_width),
                                frame_x_of(band, end.column) + static_cast<long>(band.cell_width));
    const long bottom = frame_y_of(band, end.row) + static_cast<long>(band.cell_height);
    return {start, end, span(0, top, right, bottom)};
}

std::vector<PixelRect> range_rects(Interpretation interpretation, FramePoint anchor,
                                   FramePoint focus, const ScreenBand* band,
                                   std::uint32_t frame_width) {
    switch (interpretation) {
    case Interpretation::anywhere:
        return {};
    case Interpretation::columns:
        return {snapped_rectangle(anchor, focus, band)};
    case Interpretation::rows:
        break;
    }
    if (band == nullptr || !gridded(*band)) {
        return {};
    }
    const RowsFlow flow = rows_flow(anchor, focus, *band, frame_width);
    const long left = frame_x_of(*band, 0);
    const long right = std::max(left, static_cast<long>(frame_width));
    const long pitch = band->row_pitch;
    const long start_x = frame_x_of(*band, flow.start.column);
    const long end_x = frame_x_of(*band, flow.end.column) + static_cast<long>(band->cell_width);
    const long start_y = frame_y_of(*band, flow.start.row);
    const long end_y = frame_y_of(*band, flow.end.row);
    if (flow.start.row == flow.end.row) {
        return {span(start_x, start_y, end_x, start_y + pitch)};
    }
    std::vector<PixelRect> rects{span(start_x, start_y, right, start_y + pitch)};
    if (flow.end.row - flow.start.row > 1) {
        rects.push_back(span(left, start_y + pitch, right, end_y));
    }
    rects.push_back(span(left, end_y, end_x, end_y + pitch));
    return rects;
}

RowsText trim_to_flow(const std::vector<TextRun>& runs, const ScreenBand& band,
                      const RowsFlow& flow) {
    struct Kept {
        int row;
        TextRun run;
    };
    std::vector<Kept> kept;
    for (const TextRun& run : runs) {
        const int row = row_at(band, static_cast<int>(run.bounds.y));
        if (row < flow.start.row || row > flow.end.row) {
            continue;
        }
        const std::uint32_t step = run.cell_width > 0 ? run.cell_width : band.column_pitch;
        const std::vector<std::string> glyphs = characters(run.text);
        TextRun trimmed;
        trimmed.cell_width = run.cell_width;
        trimmed.cell_height = run.cell_height;
        std::size_t first = glyphs.size();
        std::size_t count = 0;
        for (std::size_t i = 0; i < glyphs.size(); ++i) {
            const auto centre = static_cast<long>(run.bounds.x) +
                                static_cast<long>(i * step) + static_cast<long>(step / 2);
            const int column = column_at(band, static_cast<int>(centre));
            const bool inside = (row > flow.start.row || column >= flow.start.column) &&
                                (row < flow.end.row || column <= flow.end.column);
            if (!inside) {
                continue;
            }
            first = std::min(first, i);
            ++count;
            trimmed.text += glyphs[i];
            if (i < run.cells.size()) {
                trimmed.cells.push_back(run.cells[i]);
            }
        }
        if (count == 0) {
            continue;
        }
        trimmed.bounds = {run.bounds.x + static_cast<std::uint32_t>(first * step), run.bounds.y,
                          static_cast<std::uint32_t>(count * step), run.bounds.height};
        kept.push_back({row, std::move(trimmed)});
    }
    std::stable_sort(kept.begin(), kept.end(), [](const Kept& a, const Kept& b) {
        return a.row != b.row ? a.row < b.row : a.run.bounds.x < b.run.bounds.x;
    });

    RowsText result;
    for (int row = flow.start.row; row <= std::max(flow.start.row, flow.end.row); ++row) {
        if (row > flow.start.row) {
            result.text += '\n';
        }
        for (const Kept& k : kept) {
            if (k.row == row) {
                result.text += k.run.text;
            }
        }
    }
    for (Kept& k : kept) {
        result.runs.push_back(std::move(k.run));
    }
    return result;
}

std::vector<std::string> characters(const std::string& text) {
    std::vector<std::string> result;
    for (std::size_t i = 0; i < text.size();) {
        std::size_t length = 1;
        const auto lead = static_cast<unsigned char>(text[i]);
        if (lead >= 0xF0) {
            length = 4;
        } else if (lead >= 0xE0) {
            length = 3;
        } else if (lead >= 0xC0) {
            length = 2;
        }
        length = std::min(length, text.size() - i);
        result.push_back(text.substr(i, length));
        i += length;
    }
    return result;
}

std::vector<PixelRect> matched_cell_rects(const std::vector<TextRun>& runs) {
    std::vector<PixelRect> rects;
    for (const TextRun& run : runs) {
        if (run.cells.empty()) {
            rects.push_back(run.bounds);
            continue;
        }
        const std::vector<std::string> glyphs = characters(run.text);
        const auto real = [&](std::size_t i) {
            return run.cells[i].matched && i < glyphs.size() && glyphs[i] != " ";
        };
        std::size_t first = run.cells.size();
        std::size_t last = 0;
        for (std::size_t i = 0; i < run.cells.size(); ++i) {
            if (real(i)) {
                first = std::min(first, i);
                last = i;
            }
        }
        for (std::size_t i = first; i < run.cells.size() && i <= last; ++i) {
            if (run.cells[i].matched) {
                rects.push_back(run.cells[i].bounds);
            }
        }
    }
    return rects;
}

}  // namespace beebium::linux_client
