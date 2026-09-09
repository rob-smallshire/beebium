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
        queued_strings_.fetch_add(1, std::memory_order_release);
    }
    note_status_change();
    return true;
}

void TypeAheadQueue::tick_active() {
    if (cancel_requested_.exchange(false, std::memory_order_acquire)) {
        cancel_current_string();
    }

    switch (state_) {
        case State::Idle: {
            // Nothing queued after all: the cancel above was the work, or
            // clear() emptied the queue since tick() looked.
            if (queued_strings_.load(std::memory_order_acquire) == 0) {
                return;
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                current_text_ = std::move(queue_.front().text);
                current_hold_cycles_ = queue_.front().hold_cycles;
                current_gap_cycles_ = queue_.front().gap_cycles;
                queue_.pop();
                queued_strings_.fetch_sub(1, std::memory_order_release);
            }
            current_index_ = 0;
            cycle_count_ = 0;

            // Start typing first character
            if (current_index_ < current_text_.size()) {
                advance_to_next_char();
            }
            publish_current_remaining();
            note_status_change();
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

                if (current_index_ < current_text_.size()) {
                    advance_to_next_char();
                    publish_current_remaining();
                } else {
                    finish_current_string();
                }

                // A character has been consumed, and the string may have
                // finished. Either way a watcher has something to report.
                note_status_change();
            }
            break;
        }
    }
}

bool TypeAheadQueue::empty() const {
    return queued_strings_.load(std::memory_order_acquire) == 0 &&
           (current_remaining_.load(std::memory_order_acquire) == 0 ||
            cancel_requested_.load(std::memory_order_acquire));
}

size_t TypeAheadQueue::pending_characters() const {
    size_t count = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::queue<QueueEntry> temp = queue_;
        while (!temp.empty()) {
            count += temp.front().text.size();
            temp.pop();
        }
    }

    // Add what remains of the string in progress, unless it is being
    // cancelled.
    if (!cancel_requested_.load(std::memory_order_acquire)) {
        count += current_remaining_.load(std::memory_order_acquire);
    }
    return count;
}

size_t TypeAheadQueue::strings_queued() const {
    size_t count = queued_strings_.load(std::memory_order_acquire);
    if (current_remaining_.load(std::memory_order_acquire) != 0 &&
        !cancel_requested_.load(std::memory_order_acquire)) {
        count++;
    }
    return count;
}

size_t TypeAheadQueue::clear() {
    size_t count = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!queue_.empty()) {
            count += queue_.front().text.size();
            queue_.pop();
        }
        queued_strings_.store(0, std::memory_order_release);
    }

    // The string in progress is the emulator thread's to abandon. Count it
    // here; the emulator thread releases the held key on its next tick.
    if (!cancel_requested_.load(std::memory_order_acquire)) {
        count += current_remaining_.load(std::memory_order_acquire);
    }
    if (current_remaining_.load(std::memory_order_acquire) != 0) {
        cancel_requested_.store(true, std::memory_order_release);
    }

    note_status_change();
    return count;
}

void TypeAheadQueue::cancel_current_string() {
    if (state_ != State::Idle) {
        release_current_key();
    }
    finish_current_string();
}

void TypeAheadQueue::finish_current_string() {
    state_ = State::Idle;
    current_text_.clear();
    current_index_ = 0;
    cycle_count_ = 0;
    publish_current_remaining();
}

void TypeAheadQueue::publish_current_remaining() {
    current_remaining_.store(current_text_.size() - current_index_,
                             std::memory_order_release);
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
        finish_current_string();
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
        finish_current_string();
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
