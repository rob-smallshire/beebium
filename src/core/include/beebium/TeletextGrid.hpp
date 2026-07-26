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

#pragma once

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <vector>

namespace beebium {

// Which character set a teletext cell was displayed with.
enum class TeletextCellCharset : uint8_t {
    Alpha = 0,
    ContiguousGraphics = 1,
    SeparatedGraphics = 2,
};

// One character cell of a MODE 7 display, as it was actually displayed.
//
// Captured after the SAA5050 has resolved control codes, so the attributes here
// are the ones in effect at this cell rather than something a reader has to
// re-derive by simulating the control codes itself. That is what makes this a
// better source for reading the screen than the raw bytes in screen memory:
// there is no hardware-scroll offset to undo and no attribute state to infer.
struct TeletextCell {
    uint8_t character = 0;   // 7-bit SAA5050 code, as fed to the chip
    uint8_t fg = 7;
    uint8_t bg = 0;
    TeletextCellCharset charset = TeletextCellCharset::Alpha;
    bool double_height_top = false;
    bool double_height_bottom = false;
    bool concealed = false;
    bool flashing = false;
    bool cursor = false;
    bool is_control_code = false;

    bool operator==(const TeletextCell&) const = default;
};

// A captured teletext screen: a grid of resolved cells, whatever shape the
// CRTC drove the SAA5050 into.
//
// The standard MODE 7 page is 40 columns by 25 rows, but that is the display's
// shape, not the chip's: a program can drive the SAA5050 at other extents --
// 50 by 18, say -- and the grid grows to whatever was actually displayed
// rather than folding the extra cells onto the edges or dropping them. rows()
// and columns() report the captured extent; a reader that wants the standard
// page can compare against DEFAULT_ROWS and DEFAULT_COLUMNS.
//
// Double-buffered to match the pixel framebuffer, so a reader always sees a
// whole frame rather than one being written. The back buffer is filled as the
// CRTC walks the display and swapped at VSYNC.
//
// Threading: the emulation thread fills the back buffer and swaps; any other
// thread reads through snapshot(). The swap and the snapshot are serialised by
// a mutex -- held for a single frame copy, tens of microseconds at most, fifty
// times a second -- because a reader must see one whole frame and the
// alternative lock-free schemes trade that guarantee for a saving this does
// not need. set_cell() is called from the emulation thread only and is
// deliberately outside the lock, since it runs per character.
class TeletextGrid {
public:
    // The standard MODE 7 page. A grid can be smaller or larger than this; it
    // is the nominal shape, not a bound on the captured one.
    static constexpr size_t DEFAULT_COLUMNS = 40;
    static constexpr size_t DEFAULT_ROWS = 25;

    // The largest grid the capture path can express. The CRTC's horizontal and
    // vertical character counts are eight and seven bits, and the SAA5050's
    // capture counters are a byte, so nothing addressable exceeds this. Cells
    // past it cannot arise from real programming and are dropped rather than
    // let a bug grow the buffer without bound.
    static constexpr size_t MAX_COLUMNS = 256;
    static constexpr size_t MAX_ROWS = 256;

    // The frame most recently completed.
    //
    // For single-threaded use -- tests, and the emulation thread itself. Any
    // other thread must use snapshot(), which cannot observe a swap in
    // progress. A position outside the captured extent reads as a blank cell.
    [[nodiscard]] const TeletextCell& cell(size_t row, size_t column) const {
        static const TeletextCell blank;
        if (row >= m_rows || column >= m_columns) {
            return blank;
        }
        return m_front[row * m_columns + column];
    }

    // The captured extent: the display's shape this frame, not the standard
    // page. Zero on a grid that has captured nothing.
    [[nodiscard]] size_t rows() const { return m_rows; }
    [[nodiscard]] size_t columns() const { return m_columns; }

    [[nodiscard]] uint64_t frame_number() const { return m_frame_number; }

    // True when the completed frame was captured from a teletext display.
    //
    // A reader must check this: in any other screen mode the SAA5050 is not
    // fed, so the grid holds whatever was last displayed in teletext rather
    // than anything current.
    [[nodiscard]] bool active() const { return m_active; }

    // Record a cell into the frame being captured, growing the grid to include
    // it. A position beyond the addressable maximum is dropped rather than
    // clamped onto the edge, which would corrupt real cells.
    void set_cell(size_t row, size_t column, const TeletextCell& cell) {
        if (row >= MAX_ROWS || column >= MAX_COLUMNS) {
            return;
        }
        if (row >= m_back_capacity_rows || column >= m_back_capacity_columns) {
            grow_back(std::max(row + 1, m_back_capacity_rows),
                      std::max(column + 1, m_back_capacity_columns));
        }
        m_back[row * m_back_capacity_columns + column] = cell;
        m_back_rows = std::max(m_back_rows, row + 1);
        m_back_columns = std::max(m_back_columns, column + 1);
        m_captured_any = true;
    }

    // Publish the captured frame and begin the next.
    void swap() {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Publish the captured extent, compacted so a front row is exactly its
        // own width rather than the back buffer's wider allocation stride.
        m_rows = m_back_rows;
        m_columns = m_back_columns;
        m_front.assign(m_rows * m_columns, TeletextCell{});
        for (size_t row = 0; row < m_rows; ++row) {
            const TeletextCell* source = &m_back[row * m_back_capacity_columns];
            std::copy_n(source, m_columns, &m_front[row * m_columns]);
        }

        m_active = m_captured_any;
        ++m_frame_number;

        // Wipe only what this frame wrote, keeping the allocation, so the next
        // frame starts blank without discarding capacity -- and a shrinking
        // display cannot leave stale cells in the area it no longer covers.
        for (size_t row = 0; row < m_back_rows; ++row) {
            std::fill_n(&m_back[row * m_back_capacity_columns], m_back_columns,
                        TeletextCell{});
        }
        m_back_rows = 0;
        m_back_columns = 0;
        m_captured_any = false;
    }

    // A whole completed frame, taken atomically with respect to the swap. Its
    // cells are row-major, rows * columns of them.
    struct Snapshot {
        std::vector<TeletextCell> cells;
        size_t rows = 0;
        size_t columns = 0;
        uint64_t frame_number = 0;
        bool active = false;

        [[nodiscard]] const TeletextCell& cell(size_t row, size_t column) const {
            static const TeletextCell blank;
            if (row >= rows || column >= columns) {
                return blank;
            }
            return cells[row * columns + column];
        }
    };

    [[nodiscard]] Snapshot snapshot() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        Snapshot taken;
        taken.cells = m_front;  // already compacted
        taken.rows = m_rows;
        taken.columns = m_columns;
        taken.frame_number = m_frame_number;
        taken.active = m_active;
        return taken;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_front.clear();
        m_rows = 0;
        m_columns = 0;
        m_back.clear();
        m_back_rows = 0;
        m_back_columns = 0;
        m_back_capacity_rows = 0;
        m_back_capacity_columns = 0;
        m_frame_number = 0;
        m_active = false;
        m_captured_any = false;
    }

private:
    // Enlarge the back buffer's allocation to hold at least need_rows by
    // need_columns, re-striding the cells already written. Capacity only ever
    // grows, and across the whole run rather than per frame, so this fires a
    // handful of times at most -- when a display wider or taller than any
    // before it first appears.
    void grow_back(size_t need_rows, size_t need_columns) {
        std::vector<TeletextCell> next(need_rows * need_columns);
        for (size_t row = 0; row < m_back_rows; ++row) {
            const TeletextCell* source = &m_back[row * m_back_capacity_columns];
            std::copy_n(source, m_back_columns, &next[row * need_columns]);
        }
        m_back = std::move(next);
        m_back_capacity_rows = need_rows;
        m_back_capacity_columns = need_columns;
    }

    mutable std::mutex m_mutex;

    // The published frame, compacted: its stride is exactly m_columns.
    std::vector<TeletextCell> m_front;
    size_t m_rows = 0;
    size_t m_columns = 0;

    // The frame being captured. Its stride is m_back_capacity_columns, which is
    // at least m_back_columns; the extent (m_back_rows by m_back_columns) is
    // what was written this frame.
    std::vector<TeletextCell> m_back;
    size_t m_back_rows = 0;
    size_t m_back_columns = 0;
    size_t m_back_capacity_rows = 0;
    size_t m_back_capacity_columns = 0;

    uint64_t m_frame_number = 0;
    bool m_active = false;
    bool m_captured_any = false;
};

} // namespace beebium
