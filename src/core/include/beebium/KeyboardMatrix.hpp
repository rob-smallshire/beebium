// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

#include <atomic>
#include <cstdint>

namespace beebium {

// BBC Micro keyboard matrix (10 columns x 10 rows on the Model B)
//
// The BBC keyboard is a matrix of columns and rows. Each key position
// corresponds to a (row, column) pair. The Model B and B+ have 10 columns;
// the Master 128 has 13, its keypad in columns 10-12, and no start-up links
// (its boot options are in CMOS). The column decoder takes four bits, so a
// matrix has at most 16 columns; a column the machine does not have reads as
// no key pressed.
//
// Key positions:
// - Columns 0-9: Physical keyboard columns
// - Rows 0-9: Physical keyboard rows
// - Row 0, columns 0-1: SHIFT and CTRL keys
// - Row 0, columns 2-9: Startup links (DIP switches) - stored separately
//
// Startup Options (keyboard links):
// The 8 keyboard links in row 0 (columns 2-9) form a startup options byte:
//   Link 1 (col 2) = bit 7: ROM-dependent (filing system)
//   Link 2 (col 3) = bit 6: ROM-dependent
//   Link 3 (col 4) = bit 5: ROM-dependent (disc timing)
//   Link 4 (col 5) = bit 4: ROM-dependent (disc timing)
//   Link 5 (col 6) = bit 3: SHIFT-BREAK action (1=normal, 0=reversed)
//   Link 6 (col 7) = bit 2: Screen mode bit 2
//   Link 7 (col 8) = bit 1: Screen mode bit 1
//   Link 8 (col 9) = bit 0: Screen mode bit 0
//
// Active-low logic: bit SET = link broken (open), bit CLEAR = link made.
// Default value 0xFF (all links broken) = Mode 7, normal SHIFT-BREAK.
//
// Thread Safety:
// This class is thread-safe. It can be written from one thread (e.g., gRPC)
// and read from another (e.g., emulator main loop) without external locking.
// All operations use atomic memory accesses to ensure visibility and prevent
// torn reads/writes.
//
class KeyboardMatrix {
public:
    // The Model B's matrix.
    static constexpr uint8_t NUM_COLUMNS = 10;
    static constexpr uint8_t NUM_ROWS = 10;
    static constexpr uint8_t MAX_COLUMNS = 16;

    // A machine's keyboard: how many columns it has, and whether row 0,
    // columns 2-9 are the start-up links.
    struct Layout {
        uint8_t columns = NUM_COLUMNS;
        bool links = true;
    };

    // The Master 128's: 13 columns, no links.
    static constexpr Layout MASTER_128_LAYOUT{13, false};

    KeyboardMatrix() = default;
    explicit KeyboardMatrix(Layout layout)
        : column_count_(layout.columns <= MAX_COLUMNS ? layout.columns : MAX_COLUMNS),
          has_links_(layout.links) {}

    // The number of columns this keyboard has.
    uint8_t column_count() const { return column_count_; }

    // Whether this keyboard has start-up links in row 0, columns 2-9.
    bool has_links() const { return has_links_; }

    // Set a key as pressed (thread-safe)
    void key_down(uint8_t row, uint8_t column) {
        if (row < NUM_ROWS && column < column_count_) {
            // Atomic fetch_or sets the bit without racing with other operations
            columns_[column].fetch_or(static_cast<uint16_t>(1 << row),
                                      std::memory_order_release);
        }
    }

    // Set a key as released (thread-safe)
    void key_up(uint8_t row, uint8_t column) {
        if (row < NUM_ROWS && column < column_count_) {
            // Atomic fetch_and clears the bit without racing
            columns_[column].fetch_and(static_cast<uint16_t>(~(1 << row)),
                                       std::memory_order_release);
        }
    }

    // Check if a specific key is pressed (thread-safe)
    bool is_key_pressed(uint8_t row, uint8_t column) const {
        if (row < NUM_ROWS && column < column_count_) {
            return (columns_[column].load(std::memory_order_acquire) & (1 << row)) != 0;
        }
        return false;
    }

    // Read a column's state (bitmask of pressed rows) (thread-safe)
    uint16_t read_column(uint8_t column) const {
        if (column < column_count_) {
            return columns_[column].load(std::memory_order_acquire);
        }
        return 0;
    }

    // Get all keys pressed in a row (bit per column) (thread-safe snapshot)
    uint16_t get_row_state(uint8_t row) const {
        if (row >= NUM_ROWS) return 0;
        uint16_t state = 0;
        for (uint8_t col = 0; col < column_count_; ++col) {
            if (columns_[col].load(std::memory_order_acquire) & (1 << row)) {
                state |= (1 << col);
            }
        }
        return state;
    }

    // Check if any key in a column is pressed (optionally excluding row 0) (thread-safe)
    bool any_key_in_column(uint8_t column, bool exclude_row_0 = false) const {
        if (column >= column_count_) return false;
        uint16_t mask = exclude_row_0 ? 0x3FE : 0x3FF;  // Rows 1-9 or 0-9
        return (columns_[column].load(std::memory_order_acquire) & mask) != 0;
    }

    // Clear all pressed keys (thread-safe)
    void clear() {
        for (auto& col : columns_) {
            col.store(0, std::memory_order_release);
        }
    }

    // =========================================================================
    // Startup Options (keyboard links)
    // =========================================================================

    // Set raw startup options byte (thread-safe)
    // This is the 8-bit value representing all keyboard links.
    // Active-low: bit SET = link broken, bit CLEAR = link made.
    void set_startup_options(uint8_t options) {
        startup_options_.store(options, std::memory_order_release);
    }

    // Get raw startup options byte (thread-safe)
    uint8_t startup_options() const {
        return startup_options_.load(std::memory_order_acquire);
    }

    // Set startup screen mode (0-7) (thread-safe)
    // Modifies bits 0-2 of startup options, preserving other bits.
    // Mode is stored directly: mode 0 = bits cleared, mode 7 = bits set.
    // This works because is_link_made() uses active-low logic (bit clear = made),
    // and MOS XORs the readings with 7.
    void set_screen_mode(uint8_t mode) {
        if (mode > 7) mode = 7;
        uint8_t current = startup_options_.load(std::memory_order_acquire);
        uint8_t updated = (current & 0xF8) | mode;  // Store mode directly in bits 0-2
        startup_options_.store(updated, std::memory_order_release);
    }

    // Get startup screen mode from bits 0-2 (thread-safe)
    uint8_t screen_mode() const {
        return startup_options_.load(std::memory_order_acquire) & 0x07;
    }

    // Set auto-boot flag (thread-safe)
    // When enabled (true), bit 3 is cleared, reversing SHIFT-BREAK action.
    // When disabled (false), bit 3 is set (normal behavior).
    void set_auto_boot(bool enabled) {
        uint8_t current = startup_options_.load(std::memory_order_acquire);
        uint8_t updated = enabled ? (current & ~0x08) : (current | 0x08);
        startup_options_.store(updated, std::memory_order_release);
    }

    // Get auto-boot flag (thread-safe)
    // Returns true if bit 3 is cleared (reversed SHIFT-BREAK action).
    bool auto_boot() const {
        return (startup_options_.load(std::memory_order_acquire) & 0x08) == 0;
    }

    // Check if a specific link is made (closed) (thread-safe)
    // Column must be 2-9 (links 1-8). Returns false for invalid columns.
    // Note: link made = bit CLEAR in startup options (active-low).
    bool is_link_made(uint8_t column) const {
        if (!has_links_ || column < 2 || column > 9) return false;
        uint8_t bit = 9 - column;  // Column 9 = bit 0, column 2 = bit 7
        return (startup_options_.load(std::memory_order_acquire) & (1 << bit)) == 0;
    }

private:
    // Each element is a bitmask of rows pressed in that column
    // Bit N set means row N is pressed in this column
    // Using std::atomic for thread-safe access from gRPC and emulator threads
    std::atomic<uint16_t> columns_[MAX_COLUMNS] = {};
    uint8_t column_count_ = NUM_COLUMNS;
    bool has_links_ = true;

    // Startup options byte (keyboard links)
    // Default 0xFF = all links broken = Mode 7, normal SHIFT-BREAK
    std::atomic<uint8_t> startup_options_{0xFF};
};

// A machine policy whose keyboard has start-up links the server can set
// (--links, --screen-mode, --auto-boot, the preset's startup key). The
// Master's has none: its boot options are in CMOS.
template <typename Memory>
concept HasStartupLinks = requires(Memory& m, uint8_t value) {
    m.set_startup_options(value);
    m.set_screen_mode(value);
    m.set_auto_boot(bool{});
};

} // namespace beebium
