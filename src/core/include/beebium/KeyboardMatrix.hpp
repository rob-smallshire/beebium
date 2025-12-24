#pragma once

#include <cstdint>

namespace beebium {

// BBC Micro keyboard matrix (10 columns x 10 rows)
//
// The BBC keyboard is a matrix of 10 columns and 10 rows. Each key
// position corresponds to a (row, column) pair. Links (DIP switches)
// on the bottom of the keyboard also appear in this matrix.
//
// Key positions:
// - Columns 0-9: Physical keyboard columns
// - Rows 0-9: Physical keyboard rows
// - Row 0 is special: contains startup links (mode selection)
//
// This class manages the pressed state of all keys independently
// of the VIA scanning mechanism.
//
class KeyboardMatrix {
public:
    static constexpr uint8_t NUM_COLUMNS = 10;
    static constexpr uint8_t NUM_ROWS = 10;

    // Set a key as pressed
    void key_down(uint8_t row, uint8_t column) {
        if (row < NUM_ROWS && column < NUM_COLUMNS) {
            columns_[column] |= (1 << row);
        }
    }

    // Set a key as released
    void key_up(uint8_t row, uint8_t column) {
        if (row < NUM_ROWS && column < NUM_COLUMNS) {
            columns_[column] &= ~(1 << row);
        }
    }

    // Check if a specific key is pressed
    bool is_key_pressed(uint8_t row, uint8_t column) const {
        if (row < NUM_ROWS && column < NUM_COLUMNS) {
            return (columns_[column] & (1 << row)) != 0;
        }
        return false;
    }

    // Read a column's state (bitmask of pressed rows)
    uint16_t read_column(uint8_t column) const {
        if (column < NUM_COLUMNS) {
            return columns_[column];
        }
        return 0;
    }

    // Get all keys pressed in a row (bit per column)
    uint16_t get_row_state(uint8_t row) const {
        if (row >= NUM_ROWS) return 0;
        uint16_t state = 0;
        for (uint8_t col = 0; col < NUM_COLUMNS; ++col) {
            if (columns_[col] & (1 << row)) {
                state |= (1 << col);
            }
        }
        return state;
    }

    // Check if any key in a column is pressed (optionally excluding row 0)
    bool any_key_in_column(uint8_t column, bool exclude_row_0 = false) const {
        if (column >= NUM_COLUMNS) return false;
        uint16_t mask = exclude_row_0 ? 0x3FE : 0x3FF;  // Rows 1-9 or 0-9
        return (columns_[column] & mask) != 0;
    }

    // Clear all pressed keys
    void clear() {
        for (auto& col : columns_) {
            col = 0;
        }
    }

private:
    // Each element is a bitmask of rows pressed in that column
    // Bit N set means row N is pressed in this column
    uint16_t columns_[NUM_COLUMNS] = {};
};

} // namespace beebium
