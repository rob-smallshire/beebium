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

#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>

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

// A MODE 7 screen: 40 columns by 25 rows of resolved cells.
//
// Double-buffered to match the pixel framebuffer, so a reader always sees a
// whole frame rather than one being written. The back buffer is filled as the
// CRTC walks the display and swapped at VSYNC.
//
// Threading: the emulation thread fills the back buffer and swaps; any other
// thread reads through snapshot(). The swap and the snapshot are serialised by
// a mutex -- held for a single 25x40 struct copy, tens of microseconds at most,
// fifty times a second -- because a reader must see one whole frame and the
// alternative lock-free schemes trade that guarantee for a saving this does
// not need. set_cell() is called from the emulation thread only and is
// deliberately outside the lock, since it runs per character.
class TeletextGrid {
public:
    static constexpr size_t COLUMNS = 40;
    static constexpr size_t ROWS = 25;

    // The frame most recently completed.
    //
    // For single-threaded use -- tests, and the emulation thread itself. Any
    // other thread must use snapshot(), which cannot observe a swap in
    // progress.
    [[nodiscard]] const TeletextCell& cell(size_t row, size_t column) const {
        return m_front[row][column];
    }

    [[nodiscard]] uint64_t frame_number() const { return m_frame_number; }

    // True when the completed frame was captured from a MODE 7 display.
    //
    // A reader must check this: in any other screen mode the SAA5050 is not
    // fed, so the grid holds whatever was last displayed in MODE 7 rather than
    // anything current.
    [[nodiscard]] bool active() const { return m_active; }

    // Record a cell into the frame being captured. Out-of-range positions are
    // ignored rather than clamped -- a display taller or wider than 40x25 is
    // possible with unusual CRTC programming, and silently folding those cells
    // onto the edges would corrupt the grid rather than merely omit them.
    void set_cell(size_t row, size_t column, const TeletextCell& cell) {
        if (row >= ROWS || column >= COLUMNS) {
            return;
        }
        m_back[row][column] = cell;
        m_captured_any = true;
    }

    // Publish the captured frame and begin the next.
    void swap() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_front = m_back;
        m_active = m_captured_any;
        ++m_frame_number;

        // Clear rather than carry over, so a shrinking display cannot leave
        // stale cells from a previous frame in the rows it no longer covers.
        m_back = {};
        m_captured_any = false;
    }

    // A whole completed frame, taken atomically with respect to the swap.
    struct Snapshot {
        std::array<std::array<TeletextCell, COLUMNS>, ROWS> cells{};
        uint64_t frame_number = 0;
        bool active = false;

        [[nodiscard]] const TeletextCell& cell(size_t row, size_t column) const {
            return cells[row][column];
        }
    };

    [[nodiscard]] Snapshot snapshot() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        Snapshot taken;
        taken.cells = m_front;
        taken.frame_number = m_frame_number;
        taken.active = m_active;
        return taken;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_front = {};
        m_back = {};
        m_frame_number = 0;
        m_active = false;
        m_captured_any = false;
    }

private:
    using Buffer = std::array<std::array<TeletextCell, COLUMNS>, ROWS>;

    mutable std::mutex m_mutex;
    Buffer m_front{};
    Buffer m_back{};
    uint64_t m_frame_number = 0;
    bool m_active = false;
    bool m_captured_any = false;
};

} // namespace beebium
