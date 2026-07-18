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

#include <beebium/TypeAheadQueue.hpp>
#include <beebium/KeyboardMapping.hpp>

namespace beebium {

TypeAheadQueue::TypeAheadQueue(KeyboardMatrix& keyboard)
    : keyboard_(keyboard) {
}

bool TypeAheadQueue::enqueue(std::string_view text, size_t hold_cycles, size_t gap_cycles) {
    // Validate all characters before enqueuing
    if (!is_typeable(text)) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(QueueEntry{std::string(text), hold_cycles, gap_cycles});
    }
    note_status_change();
    return true;
}

void TypeAheadQueue::tick() {
    switch (state_) {
        case State::Idle: {
            // Check for pending work
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (queue_.empty()) {
                    return;
                }
                // Pop next string from queue
                current_text_ = std::move(queue_.front().text);
                current_hold_cycles_ = queue_.front().hold_cycles;
                current_gap_cycles_ = queue_.front().gap_cycles;
                queue_.pop();
            }
            current_index_ = 0;
            cycle_count_ = 0;
            note_status_change();

            // Start typing first character
            if (current_index_ < current_text_.size()) {
                advance_to_next_char();
            }
            break;
        }

        case State::KeyDown: {
            cycle_count_++;
            if (cycle_count_ >= current_hold_cycles_) {
                // Release key after the key-down hold time
                release_current_key();
                cycle_count_ = 0;
                state_ = State::KeyUp;
            }
            break;
        }

        case State::KeyUp: {
            cycle_count_++;
            if (cycle_count_ >= current_gap_cycles_) {
                // Move to next character after the key-up gap time
                current_index_++;
                cycle_count_ = 0;

                // A character has been consumed, and the string may have
                // finished. Either way a watcher has something to report.
                note_status_change();

                if (current_index_ < current_text_.size()) {
                    advance_to_next_char();
                } else {
                    // String complete, go back to idle
                    state_ = State::Idle;
                }
            }
            break;
        }
    }
}

bool TypeAheadQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty() && state_ == State::Idle;
}

size_t TypeAheadQueue::pending_characters() const {
    std::lock_guard<std::mutex> lock(mutex_);

    size_t count = 0;

    // Count characters in queued strings
    std::queue<QueueEntry> temp = queue_;
    while (!temp.empty()) {
        count += temp.front().text.size();
        temp.pop();
    }

    // Add remaining characters in current string (if any)
    if (state_ != State::Idle && current_index_ < current_text_.size()) {
        count += current_text_.size() - current_index_;
    }

    return count;
}

size_t TypeAheadQueue::strings_queued() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = queue_.size();
    // Include current string if we're typing
    if (state_ != State::Idle) {
        count++;
    }
    return count;
}

size_t TypeAheadQueue::clear() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Count characters being cleared
    size_t count = 0;
    while (!queue_.empty()) {
        count += queue_.front().text.size();
        queue_.pop();
    }

    // Add remaining characters in current string
    if (state_ != State::Idle && current_index_ < current_text_.size()) {
        count += current_text_.size() - current_index_;

        // Release any held keys
        release_current_key();
    }

    // Reset state
    state_ = State::Idle;
    current_text_.clear();
    current_index_ = 0;
    cycle_count_ = 0;

    note_status_change();
    return count;
}

void TypeAheadQueue::advance_to_next_char() {
    // Decode UTF-8 character at current position
    char32_t codepoint;
    uint8_t byte = static_cast<uint8_t>(current_text_[current_index_]);

    if ((byte & 0x80) == 0) {
        // Single byte ASCII
        codepoint = byte;
    } else if ((byte & 0xE0) == 0xC0) {
        // Two-byte UTF-8
        codepoint = ((byte & 0x1F) << 6) |
                    (static_cast<uint8_t>(current_text_[current_index_ + 1]) & 0x3F);
        // Adjust index for multi-byte sequence (will be incremented after release)
        // Actually, we track by byte position, so we need to skip extra bytes
    } else if ((byte & 0xF0) == 0xE0) {
        // Three-byte UTF-8
        codepoint = ((byte & 0x0F) << 12) |
                    ((static_cast<uint8_t>(current_text_[current_index_ + 1]) & 0x3F) << 6) |
                    (static_cast<uint8_t>(current_text_[current_index_ + 2]) & 0x3F);
    } else if ((byte & 0xF8) == 0xF0) {
        // Four-byte UTF-8
        codepoint = ((byte & 0x07) << 18) |
                    ((static_cast<uint8_t>(current_text_[current_index_ + 1]) & 0x3F) << 12) |
                    ((static_cast<uint8_t>(current_text_[current_index_ + 2]) & 0x3F) << 6) |
                    (static_cast<uint8_t>(current_text_[current_index_ + 3]) & 0x3F);
    } else {
        // Invalid UTF-8 - shouldn't happen if is_typeable passed
        state_ = State::Idle;
        return;
    }

    // Skip extra bytes of multi-byte sequences
    // (current_index_ will be incremented by 1 after key up, so skip n-1 now)
    if ((byte & 0xE0) == 0xC0) {
        current_index_ += 1;  // Will be +2 total after post-increment
    } else if ((byte & 0xF0) == 0xE0) {
        current_index_ += 2;  // Will be +3 total after post-increment
    } else if ((byte & 0xF8) == 0xF0) {
        current_index_ += 3;  // Will be +4 total after post-increment
    }

    // Look up the mapping
    auto mapping = char_to_key(codepoint);
    if (!mapping) {
        // Shouldn't happen if is_typeable passed
        state_ = State::Idle;
        return;
    }

    current_ik_number_ = mapping->ik_number;
    current_needs_shift_ = mapping->needs_shift;

    // Press SHIFT first if needed
    if (current_needs_shift_) {
        keyboard_.key_down(0, 0);  // SHIFT is row 0, column 0
    }

    // Press the key
    keyboard_.key_down(mapping->row(), mapping->column());

    state_ = State::KeyDown;
}

void TypeAheadQueue::release_current_key() {
    // Extract row and column from stored ik_number
    uint8_t row = (current_ik_number_ >> 4) & 0x0F;
    uint8_t column = current_ik_number_ & 0x0F;

    // Release the key
    keyboard_.key_up(row, column);

    // Release SHIFT if it was pressed
    if (current_needs_shift_) {
        keyboard_.key_up(0, 0);  // SHIFT is row 0, column 0
    }
}

} // namespace beebium
